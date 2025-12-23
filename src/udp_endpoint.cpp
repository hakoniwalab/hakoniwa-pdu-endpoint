#include "hakoniwa/pdu/udp_endpoint.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <netdb.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

namespace hakoniwa {
namespace pdu {

namespace {

HakoPduErrorType map_errno_to_error(int error_number) noexcept
{
    if (error_number == EAGAIN || error_number == EWOULDBLOCK) {
        return HAKO_PDU_ERR_TIMEOUT;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}

HakoPduEndpointDirectionType parse_direction(const std::string& direction)
{
    if (direction == "in") {
        return HAKO_PDU_ENDPOINT_DIRECTION_IN;
    }
    if (direction == "out") {
        return HAKO_PDU_ENDPOINT_DIRECTION_OUT;
    }
    return HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
}

HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json, addrinfo** res)
{
    if (!endpoint_json.contains("address") || !endpoint_json.contains("port")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const std::string address = endpoint_json.at("address").get<std::string>();
    const int port = endpoint_json.at("port").get<int>();
    const std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE; 

    if (getaddrinfo(address.c_str(), port_str.c_str(), &hints, res) != 0 || *res == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return HAKO_PDU_ERR_OK;
}

}  // namespace

UdpEndpoint::UdpEndpoint(const std::string& name, HakoPduEndpointDirectionType type)
    : Endpoint(name, type) {}

HakoPduErrorType UdpEndpoint::open(const std::string& config_path)
{
    if (socket_fd_ != -1) {
        return HAKO_PDU_ERR_BUSY;
    }

    std::ifstream config_stream(config_path);
    if (!config_stream) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    nlohmann::json config_json;
    try {
        config_stream >> config_json;
    } catch (const nlohmann::json::exception& e) {
        // std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "udp") {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("direction")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());

    addrinfo* local_addr_info = nullptr;
    addrinfo* remote_addr_info = nullptr;

    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        if (!config_json.contains("local")) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (resolve_address(config_json.at("local"), &local_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        if (!config_json.contains("remote")) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (resolve_address(config_json.at("remote"), &remote_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    } else if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT && config_json.contains("remote")) {
        if (resolve_address(config_json.at("remote"), &remote_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        has_fixed_remote_ = true;
    }

    addrinfo* initial_addr = local_addr_info ? local_addr_info : remote_addr_info;
    if (!initial_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    
    socket_fd_ = ::socket(initial_addr->ai_family, initial_addr->ai_socktype, initial_addr->ai_protocol);
    if (socket_fd_ < 0) {
        if(local_addr_info) freeaddrinfo(local_addr_info);
        if(remote_addr_info) freeaddrinfo(remote_addr_info);
        return HAKO_PDU_ERR_IO_ERROR;
    }

    Options options;
    if (config_json.contains("options")) {
        const auto& opts = config_json.at("options");
        options.buffer_size = opts.value("buffer_size", 8192);
        options.timeout_ms = opts.value("timeout_ms", 1000);
        options.blocking = opts.value("blocking", true);
        options.reuse_address = opts.value("reuse_address", true);
        options.broadcast = opts.value("broadcast", false);
        if (opts.contains("multicast")) {
            const auto& mc = opts.at("multicast");
            options.multicast_enabled = mc.value("enabled", false);
            if(options.multicast_enabled) {
                options.multicast_group = mc.value("group", "");
                options.multicast_interface = mc.value("interface", "0.0.0.0");
                options.multicast_ttl = mc.value("ttl", 1);
            }
        }
    }

    HakoPduErrorType option_result = configure_socket_options(options);
    if (option_result != HAKO_PDU_ERR_OK) {
        close();
        return option_result;
    }

    if (local_addr_info) {
        if (::bind(socket_fd_, local_addr_info->ai_addr, local_addr_info->ai_addrlen) != 0) {
            freeaddrinfo(local_addr_info);
            if(remote_addr_info) freeaddrinfo(remote_addr_info);
            close();
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (remote_addr_info) {
        std::memcpy(&dest_addr_, remote_addr_info->ai_addr, remote_addr_info->ai_addrlen);
        dest_addr_len_ = remote_addr_info->ai_addrlen;
    }

    if (local_addr_info) freeaddrinfo(local_addr_info);
    if (remote_addr_info) freeaddrinfo(remote_addr_info);

    if (options.multicast_enabled) {
        HakoPduErrorType multicast_result = configure_multicast(options);
        if (multicast_result != HAKO_PDU_ERR_OK) {
            close();
            return multicast_result;
        }
    }

    running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::close() noexcept
{
    running_ = false;
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    has_fixed_remote_ = false;
    dest_addr_len_ = 0;
    last_client_addr_len_ = 0;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::start() noexcept
{
    if (socket_fd_ < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    running_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::stop() noexcept
{
    running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::is_running(bool& running) noexcept
{
    running = running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::send(const void* data, size_t size) noexcept
{
    if (socket_fd_ < 0 || data == nullptr || size == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    const sockaddr* target_addr = nullptr;
    socklen_t target_addr_len = 0;

    if (has_fixed_remote_) {
        target_addr = reinterpret_cast<const sockaddr*>(&dest_addr_);
        target_addr_len = dest_addr_len_;
    } else if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        target_addr = reinterpret_cast<const sockaddr*>(&last_client_addr_);
        target_addr_len = last_client_addr_len_;
        if (target_addr_len == 0) {
            // Not received yet, can't send
            return HAKO_PDU_ERR_IO_ERROR; 
        }
    } else { // OUT
        target_addr = reinterpret_cast<const sockaddr*>(&dest_addr_);
        target_addr_len = dest_addr_len_;
    }

    if (target_addr == nullptr || target_addr_len == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    ssize_t sent = ::sendto(socket_fd_, data, size, 0, target_addr, target_addr_len);
    if (sent < 0) {
        return map_errno_to_error(errno);
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::recv(void* data, size_t buffer_size, size_t& received_size) noexcept
{
    received_size = 0;
    if (socket_fd_ < 0 || data == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    sockaddr_storage from{};
    socklen_t from_len = sizeof(from);
    ssize_t received = ::recvfrom(socket_fd_, data, buffer_size, 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);

    if (received < 0) {
        return map_errno_to_error(errno);
    }
    
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT && !has_fixed_remote_) {
        std::memcpy(&last_client_addr_, &from, from_len);
        last_client_addr_len_ = from_len;
    }

    received_size = static_cast<size_t>(received);
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::configure_socket_options(const Options& options) noexcept
{
    if (options.reuse_address) {
        int reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.broadcast) {
        int broadcast = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.buffer_size > 0) {
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &options.buffer_size, sizeof(options.buffer_size)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.timeout_ms >= 0) {
        timeval timeout{};
        timeout.tv_sec = options.timeout_ms / 1000;
        timeout.tv_usec = (options.timeout_ms % 1000) * 1000;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (!options.blocking) {
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpEndpoint::configure_multicast(const Options& options) noexcept
{
    if (options.multicast_group.empty()) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    
    // Membership is for IN endpoints
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        ip_mreq mreq{};
        if (inet_pton(AF_INET, options.multicast_group.c_str(), &mreq.imr_multiaddr) != 1) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (inet_pton(AF_INET, options.multicast_interface.c_str(), &mreq.imr_interface) != 1) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    
    // TTL is for OUT endpoints
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &options.multicast_ttl, sizeof(options.multicast_ttl)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa
