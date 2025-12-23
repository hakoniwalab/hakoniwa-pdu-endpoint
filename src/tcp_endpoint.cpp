#include "hakoniwa/pdu/tcp_endpoint.hpp"

#include "hakoniwa/pdu/socket_utils.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <netinet/tcp.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace hakoniwa {
namespace pdu {

namespace {
constexpr int kTcpSocketType = SOCK_STREAM;
}  // namespace

TcpEndpoint::TcpEndpoint(const std::string& name, HakoPduEndpointDirectionType type)
    : Endpoint(name, type) {}

HakoPduErrorType TcpEndpoint::open(const std::string& config_path)
{
    if (socket_fd_ != -1 || listen_fd_ != -1) {
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

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "tcp") {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("direction") || !config_json.contains("role")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());
    const std::string role_value = config_json.at("role").get<std::string>();
    if (role_value == "server") {
        role_ = Role::Server;
    } else if (role_value == "client") {
        role_ = Role::Client;
    } else {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    addrinfo* local_addr_info = nullptr;
    addrinfo* remote_addr_info = nullptr;

    if (role_ == Role::Server) {
        if (!config_json.contains("local")) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("local"), kTcpSocketType, &local_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }
    if (role_ == Role::Client) {
        if (!config_json.contains("remote")) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("remote"), kTcpSocketType, &remote_addr_info) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }

    if (config_json.contains("options")) {
        const auto& opts = config_json.at("options");
        options_.backlog = opts.value("backlog", options_.backlog);
        options_.connect_timeout_ms = opts.value("connect_timeout_ms", options_.connect_timeout_ms);
        options_.read_timeout_ms = opts.value("read_timeout_ms", options_.read_timeout_ms);
        options_.write_timeout_ms = opts.value("write_timeout_ms", options_.write_timeout_ms);
        options_.blocking = opts.value("blocking", options_.blocking);
        options_.reuse_address = opts.value("reuse_address", options_.reuse_address);
        options_.keepalive = opts.value("keepalive", options_.keepalive);
        options_.no_delay = opts.value("no_delay", options_.no_delay);
        options_.recv_buffer_size = opts.value("recv_buffer_size", options_.recv_buffer_size);
        options_.send_buffer_size = opts.value("send_buffer_size", options_.send_buffer_size);
        if (opts.contains("linger")) {
            const auto& linger_opts = opts.at("linger");
            options_.linger_enabled = linger_opts.value("enabled", options_.linger_enabled);
            options_.linger_timeout_sec = linger_opts.value("timeout_sec", options_.linger_timeout_sec);
        }
    }

    addrinfo* initial_addr = local_addr_info ? local_addr_info : remote_addr_info;
    if (!initial_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (role_ == Role::Server) {
        listen_fd_ = ::socket(initial_addr->ai_family, initial_addr->ai_socktype, initial_addr->ai_protocol);
        if (listen_fd_ < 0) {
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (configure_socket_options(listen_fd_, options_) != HAKO_PDU_ERR_OK) {
            close();
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (::bind(listen_fd_, local_addr_info->ai_addr, local_addr_info->ai_addrlen) != 0) {
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            close();
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (::listen(listen_fd_, options_.backlog) != 0) {
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            close();
            return HAKO_PDU_ERR_IO_ERROR;
        }
        std::memcpy(&local_addr_, local_addr_info->ai_addr, local_addr_info->ai_addrlen);
        local_addr_len_ = local_addr_info->ai_addrlen;
    } else {
        socket_fd_ = ::socket(initial_addr->ai_family, initial_addr->ai_socktype, initial_addr->ai_protocol);
        if (socket_fd_ < 0) {
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (configure_socket_options(socket_fd_, options_) != HAKO_PDU_ERR_OK) {
            close();
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (connect_with_timeout(socket_fd_, remote_addr_info, options_) != HAKO_PDU_ERR_OK) {
            close();
            if (local_addr_info) freeaddrinfo(local_addr_info);
            if (remote_addr_info) freeaddrinfo(remote_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        std::memcpy(&remote_addr_, remote_addr_info->ai_addr, remote_addr_info->ai_addrlen);
        remote_addr_len_ = remote_addr_info->ai_addrlen;
    }

    if (local_addr_info) freeaddrinfo(local_addr_info);
    if (remote_addr_info) freeaddrinfo(remote_addr_info);

    running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::close() noexcept
{
    running_ = false;
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    local_addr_len_ = 0;
    remote_addr_len_ = 0;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::start() noexcept
{
    if (role_ == Role::Server) {
        if (listen_fd_ < 0) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    } else if (socket_fd_ < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    running_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::stop() noexcept
{
    running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::is_running(bool& running) noexcept
{
    running = running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::send(const void* data, size_t size) noexcept
{
    if (data == nullptr || size == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    HakoPduErrorType connect_result = ensure_connected();
    if (connect_result != HAKO_PDU_ERR_OK) {
        return connect_result;
    }

    size_t total_sent = 0;
    const auto* buffer = static_cast<const char*>(data);
    while (total_sent < size) {
        ssize_t sent = ::send(socket_fd_, buffer + total_sent, size - total_sent, 0);
        if (sent > 0) {
            total_sent += static_cast<size_t>(sent);
            continue;
        }
        if (sent == 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (errno == EBUSY || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return map_errno_to_error(errno);
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::recv(void* data, size_t buffer_size, size_t& received_size) noexcept
{
    received_size = 0;
    if (data == nullptr || buffer_size == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    HakoPduErrorType connect_result = ensure_connected();
    if (connect_result != HAKO_PDU_ERR_OK) {
        return connect_result;
    }

    size_t total_received = 0;
    auto* buffer = static_cast<char*>(data);
    while (total_received < buffer_size) {
        ssize_t received = ::recv(socket_fd_, buffer + total_received, buffer_size - total_received, 0);
        if (received > 0) {
            total_received += static_cast<size_t>(received);
            continue;
        }
        if (received == 0) {
            received_size = total_received;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (errno == EBUSY || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        received_size = total_received;
        return map_errno_to_error(errno);
    }

    received_size = total_received;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::configure_socket_options(int fd, const Options& options) noexcept
{
    if (options.reuse_address) {
        int reuse = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.keepalive) {
        int keepalive = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.no_delay) {
        int nodelay = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.recv_buffer_size > 0) {
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &options.recv_buffer_size, sizeof(options.recv_buffer_size)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.send_buffer_size > 0) {
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &options.send_buffer_size, sizeof(options.send_buffer_size)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (options.linger_enabled) {
        linger linger_opts{};
        linger_opts.l_onoff = 1;
        linger_opts.l_linger = options.linger_timeout_sec;
        if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger_opts, sizeof(linger_opts)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    return configure_timeouts(fd, options);
}

HakoPduErrorType TcpEndpoint::configure_timeouts(int fd, const Options& options) noexcept
{
    if (options.read_timeout_ms >= 0) {
        timeval timeout{};
        timeout.tv_sec = options.read_timeout_ms / 1000;
        timeout.tv_usec = (options.read_timeout_ms % 1000) * 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.write_timeout_ms >= 0) {
        timeval timeout{};
        timeout.tv_sec = options.write_timeout_ms / 1000;
        timeout.tv_usec = (options.write_timeout_ms % 1000) * 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    if (!options.blocking) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::connect_with_timeout(int fd, addrinfo* remote_addr, const Options& options) noexcept
{
    if (!remote_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    bool restore_blocking = options.blocking;
    if (options.connect_timeout_ms > 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        int connect_result = ::connect(fd, remote_addr->ai_addr, remote_addr->ai_addrlen);
        if (connect_result == 0) {
            if (restore_blocking) {
                fcntl(fd, F_SETFL, flags);
            }
            return HAKO_PDU_ERR_OK;
        }
        if (errno != EINPROGRESS) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        pollfd poll_fd{};
        poll_fd.fd = fd;
        poll_fd.events = POLLOUT;
        int poll_result = ::poll(&poll_fd, 1, options.connect_timeout_ms);
        if (poll_result <= 0) {
            return HAKO_PDU_ERR_TIMEOUT;
        }

        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 || so_error != 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        if (restore_blocking) {
            fcntl(fd, F_SETFL, flags);
        }

        return HAKO_PDU_ERR_OK;
    }

    if (::connect(fd, remote_addr->ai_addr, remote_addr->ai_addrlen) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::accept_client() noexcept
{
    if (listen_fd_ < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (socket_fd_ >= 0) {
        return HAKO_PDU_ERR_OK;
    }

    if (options_.read_timeout_ms >= 0) {
        pollfd poll_fd{};
        poll_fd.fd = listen_fd_;
        poll_fd.events = POLLIN;
        int poll_result = ::poll(&poll_fd, 1, options_.read_timeout_ms);
        if (poll_result == 0) {
            return HAKO_PDU_ERR_TIMEOUT;
        }
        if (poll_result < 0) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    sockaddr_storage client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        return map_errno_to_error(errno);
    }

    if (configure_socket_options(client_fd, options_) != HAKO_PDU_ERR_OK) {
        ::close(client_fd);
        return HAKO_PDU_ERR_IO_ERROR;
    }

    socket_fd_ = client_fd;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpEndpoint::ensure_connected() noexcept
{
    if (role_ == Role::Server) {
        return accept_client();
    }

    if (socket_fd_ < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa
