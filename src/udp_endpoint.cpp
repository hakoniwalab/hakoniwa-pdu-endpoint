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

namespace hakoniwa {
namespace pdu {

namespace {

constexpr int kDefaultBufferSize = 8192;
constexpr int kDefaultTimeoutMs = 1000;

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

bool is_direction_compatible(HakoPduEndpointDirectionType configured,
                             HakoPduEndpointDirectionType requested)
{
    if (configured == requested) {
        return true;
    }
    return configured == HAKO_PDU_ENDPOINT_DIRECTION_INOUT
        || requested == HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
}

}  // namespace

UdpEndpoint::UdpEndpoint(const std::string& name, HakoPduEndpointDirectionType type)
    : Endpoint(name, type)
{
}

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
    } catch (const nlohmann::json::exception&) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "udp") {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("direction") || !config_json.contains("address") || !config_json.contains("port")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    const std::string direction_string = config_json.at("direction").get<std::string>();
    config_direction_ = parse_direction(direction_string);
    if (!is_direction_compatible(config_direction_, type_)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    const std::string address = config_json.at("address").get<std::string>();
    const int port = config_json.at("port").get<int>();

    Options options;
    options.buffer_size = kDefaultBufferSize;
    options.timeout_ms = kDefaultTimeoutMs;

    if (config_json.contains("options")) {
        const auto& opts = config_json.at("options");
        if (opts.contains("buffer_size")) {
            options.buffer_size = opts.at("buffer_size").get<int>();
        }
        if (opts.contains("timeout_ms")) {
            options.timeout_ms = opts.at("timeout_ms").get<int>();
        }
        if (opts.contains("blocking")) {
            options.blocking = opts.at("blocking").get<bool>();
        }
        if (opts.contains("reuse_address")) {
            options.reuse_address = opts.at("reuse_address").get<bool>();
        }
        if (opts.contains("broadcast")) {
            options.broadcast = opts.at("broadcast").get<bool>();
        }
        if (opts.contains("multicast")) {
            const auto& mc = opts.at("multicast");
            if (mc.contains("enabled")) {
                options.multicast_enabled = mc.at("enabled").get<bool>();
            }
            if (mc.contains("group")) {
                options.multicast_group = mc.at("group").get<std::string>();
            }
            if (mc.contains("interface")) {
                options.multicast_interface = mc.at("interface").get<std::string>();
            }
            if (mc.contains("ttl")) {
                options.multicast_ttl = mc.at("ttl").get<int>();
            }
        }
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(address.c_str(), port_str.c_str(), &hints, &result) != 0 || result == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    socket_fd_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_fd_ < 0) {
        freeaddrinfo(result);
        return HAKO_PDU_ERR_IO_ERROR;
    }

    HakoPduErrorType option_result = configure_socket_options(options);
    if (option_result != HAKO_PDU_ERR_OK) {
        freeaddrinfo(result);
        close();
        return option_result;
    }

    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN ||
        config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_INOUT) {
        if (::bind(socket_fd_, result->ai_addr, result->ai_addrlen) != 0) {
            freeaddrinfo(result);
            close();
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    dest_addr_len_ = static_cast<socklen_t>(result->ai_addrlen);
    std::memset(&dest_addr_, 0, sizeof(dest_addr_));
    std::memcpy(&dest_addr_, result->ai_addr, result->ai_addrlen);

    freeaddrinfo(result);

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
    if (socket_fd_ < 0 || data == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (type_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    ssize_t sent = ::sendto(socket_fd_, data, size, 0,
                            reinterpret_cast<const sockaddr*>(&dest_addr_), dest_addr_len_);
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
    if (type_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    sockaddr_storage from{};
    socklen_t from_len = sizeof(from);
    ssize_t received = ::recvfrom(socket_fd_, data, buffer_size, 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received < 0) {
        return map_errno_to_error(errno);
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
        int buffer_size = options.buffer_size;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) != 0) {
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

    sockaddr_storage local_addr{};
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr), &local_len) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    if (local_addr.ss_family != AF_INET) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

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

    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &options.multicast_ttl,
                   sizeof(options.multicast_ttl)) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa
