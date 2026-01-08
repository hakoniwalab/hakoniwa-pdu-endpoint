#include "hakoniwa/pdu/comm/comm_udp.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
constexpr int kUdpSocketType = SOCK_DGRAM;
}  // namespace

UdpComm::UdpComm()
{
    // Constructor
}

UdpComm::~UdpComm()
{
    raw_close(); // Call the raw_close method for cleanup
}

HakoPduErrorType UdpComm::raw_open(const std::string& config_path)
{
    if (socket_fd_.load() != -1) {
        return HAKO_PDU_ERR_BUSY;
    }

    std::ifstream config_stream(config_path);
    if (!config_stream) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    nlohmann::json config_json;
    try {
        config_stream >> config_json;
    } catch (const nlohmann::json::exception&) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "udp") {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("direction")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());
    
    if (!config_json.contains("pdu_key")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const auto& key_json = config_json.at("pdu_key");
    pdu_key_.robot = key_json.value("robot", "");
    pdu_key_.channel_id = key_json.value("channel_id", 0);


    addrinfo* local_addr_info = nullptr;
    addrinfo* remote_addr_info = nullptr;

    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        if (!config_json.contains("local")) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (resolve_address(config_json.at("local"), kUdpSocketType, &local_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        if (!config_json.contains("remote")) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (resolve_address(config_json.at("remote"), kUdpSocketType, &remote_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    } else if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT && config_json.contains("remote")) {
        if (resolve_address(config_json.at("remote"), kUdpSocketType, &remote_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        has_fixed_remote_ = true;
    }

    addrinfo* initial_addr = local_addr_info ? local_addr_info : remote_addr_info;
    if (!initial_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    
    socket_fd_ = ::socket(initial_addr->ai_family, initial_addr->ai_socktype, initial_addr->ai_protocol);
    if (socket_fd_.load() < 0) {
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

    // Set timeout for recv_loop
    if (options.timeout_ms <= 0) {
        options.timeout_ms = 1000; // 1 sec default
    }

    HakoPduErrorType option_result = configure_socket_options(options);
    if (option_result != HAKO_PDU_ERR_OK) {
        raw_close(); // Use raw_close for cleanup
        if(local_addr_info) freeaddrinfo(local_addr_info);
        if(remote_addr_info) freeaddrinfo(remote_addr_info);
        return option_result;
    }

    if (local_addr_info) {
        if (::bind(socket_fd_.load(), local_addr_info->ai_addr, local_addr_info->ai_addrlen) != 0) {
            raw_close(); // Use raw_close for cleanup
            freeaddrinfo(local_addr_info);
            if(remote_addr_info) freeaddrinfo(remote_addr_info);
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
            raw_close(); // Use raw_close for cleanup
            return multicast_result;
        }
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::raw_close() noexcept
{
    raw_stop(); // Stop the thread
    int current_socket_fd = socket_fd_.load();
    if (current_socket_fd >= 0) {
        ::close(current_socket_fd);
        socket_fd_ = -1;
    }
    has_fixed_remote_ = false;
    dest_addr_len_ = 0;
    last_client_addr_len_ = 0;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::raw_start() noexcept
{
    if (socket_fd_.load() < 0 || is_running_flag_) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        // No thread for OUT direction
        is_running_flag_ = true;
        return HAKO_PDU_ERR_OK;
    }
    is_running_flag_ = true;
    recv_thread_ = std::thread(&UdpComm::recv_loop, this);
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::raw_stop() noexcept
{
    if (!is_running_flag_) {
        return HAKO_PDU_ERR_OK;
    }
    is_running_flag_ = false;

    if (recv_thread_.joinable()) {
        // Unblock recvfrom by shutting down the read part of the socket
        int current_socket_fd = socket_fd_.load();
        if (current_socket_fd >= 0) {
             ::shutdown(current_socket_fd, SHUT_RD);
        }
        recv_thread_.join();
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::raw_is_running(bool& running) noexcept
{
    running = is_running_flag_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::raw_send(const std::vector<std::byte>& data) noexcept
{
    int current_socket_fd = socket_fd_.load();
    if (current_socket_fd < 0 || data.empty()) {
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
        if (last_client_addr_len_ == 0) return HAKO_PDU_ERR_IO_ERROR; // Not received yet
        target_addr = reinterpret_cast<const sockaddr*>(&last_client_addr_);
        target_addr_len = last_client_addr_len_;
    } else { // OUT
        target_addr = reinterpret_cast<const sockaddr*>(&dest_addr_);
        target_addr_len = dest_addr_len_;
    }

    if (target_addr == nullptr || target_addr_len == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    ssize_t sent = ::sendto(current_socket_fd, data.data(), data.size(), 0, target_addr, target_addr_len);
    if (sent < 0) {
        return map_errno_to_error(errno);
    }
    return HAKO_PDU_ERR_OK;
}

// UdpComm's recv method is removed as it's now handled by PduCommRaw.

void UdpComm::recv_loop()
{
    std::vector<std::byte> buffer(65536); // Max UDP packet size
    while (is_running_flag_) {
        sockaddr_storage from{};
        socklen_t from_len = sizeof(from);
        ssize_t received = ::recvfrom(socket_fd_.load(), buffer.data(), buffer.size(), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // Timeout or interrupted, just continue loop
                continue;
            }
            if (!is_running_flag_) {
                // Expected error after stop() called shutdown()
                break;
            }
            // Real error
            // std::cerr << "recvfrom error: " << strerror(errno) << std::endl;
            continue;
        }

        if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT && !has_fixed_remote_) {
            std::memcpy(&last_client_addr_, &from, from_len);
            last_client_addr_len_ = from_len;
        }

        // Call the base class's method to handle raw data
        on_raw_data_received({buffer.begin(), buffer.begin() + received});
    }
}


HakoPduErrorType UdpComm::configure_socket_options(const Options& options) noexcept
{
    if (options.reuse_address) {
        int reuse = 1;
        if (setsockopt(socket_fd_.load(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.broadcast) {
        int broadcast = 1;
        if (setsockopt(socket_fd_.load(), SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.buffer_size > 0) {
        if (setsockopt(socket_fd_.load(), SOL_SOCKET, SO_RCVBUF, &options.buffer_size, sizeof(options.buffer_size)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    timeval timeout{};
    timeout.tv_sec = options.timeout_ms / 1000;
    timeout.tv_usec = (options.timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_.load(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (setsockopt(socket_fd_.load(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (!options.blocking) {
        int flags = fcntl(socket_fd_.load(), F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd_.load(), F_SETFL, flags | O_NONBLOCK) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType UdpComm::configure_multicast(const Options& options) noexcept
{
    if (options.multicast_group.empty()) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        ip_mreq mreq{};
        if (inet_pton(AF_INET, options.multicast_group.c_str(), &mreq.imr_multiaddr) != 1) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (inet_pton(AF_INET, options.multicast_interface.c_str(), &mreq.imr_interface) != 1) return HAKO_PDU_ERR_INVALID_ARGUMENT;
        if (setsockopt(socket_fd_.load(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) return HAKO_PDU_ERR_IO_ERROR;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT || config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        if (setsockopt(socket_fd_.load(), IPPROTO_IP, IP_MULTICAST_TTL, &options.multicast_ttl, sizeof(options.multicast_ttl)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    return HAKO_PDU_ERR_OK;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
