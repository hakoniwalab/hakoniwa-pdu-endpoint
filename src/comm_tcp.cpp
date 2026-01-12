#include "hakoniwa/pdu/comm/comm_tcp.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <iostream>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
constexpr int kTcpSocketType = SOCK_STREAM;
}

TcpComm::TcpComm() {}
TcpComm::~TcpComm() {
    raw_close();
}

HakoPduErrorType TcpComm::raw_open(const std::string& config_path) {
    if (client_fd_.load() != -1 || listen_fd_.load() != -1) {
        return HAKO_PDU_ERR_BUSY;
    }

    std::ifstream config_stream(config_path);
    if (!config_stream) {
        std::cerr << "Failed to open TCP Comm config file: " << config_path << std::endl;
        return HAKO_PDU_ERR_IO_ERROR;
    }

    nlohmann::json config_json;
    try {
        config_stream >> config_json;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "TCP Comm config JSON parse error: " << e.what() << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "tcp") {
        std::cerr << "TCP Comm config error: protocol is not 'tcp'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("direction") || !config_json.contains("role")) {
        std::cerr << "TCP Comm config error: missing 'direction' or 'role'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());
    
    const std::string role_value = config_json.at("role").get<std::string>();
    if (role_value == "server") {
        role_ = Role::Server;
    } else if (role_value == "client") {
        role_ = Role::Client;
    } else {
        std::cerr << "TCP Comm config error: unknown role '" << role_value << "'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
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

    if (role_ == Role::Server) {
        addrinfo* local_addr_info = nullptr;
        if (!config_json.contains("local")) {
            std::cerr << "TCP Comm config error: missing 'local' for server role." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("local"), kTcpSocketType, &local_addr_info) != HAKO_PDU_ERR_OK) {
            std::cerr << "TCP Comm config error: failed to resolve local address." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }

        listen_fd_ = ::socket(local_addr_info->ai_family, local_addr_info->ai_socktype, local_addr_info->ai_protocol);
        if (listen_fd_.load() < 0) {
            freeaddrinfo(local_addr_info);
            std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (configure_socket_options(listen_fd_.load(), options_) != HAKO_PDU_ERR_OK) {
            raw_close();
            freeaddrinfo(local_addr_info);
            std::cerr << "Failed to configure socket options." << std::endl;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (::bind(listen_fd_.load(), local_addr_info->ai_addr, local_addr_info->ai_addrlen) != 0) {
            raw_close();
            freeaddrinfo(local_addr_info);
            std::cerr << "Failed to bind socket: " << std::strerror(errno) << std::endl;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (::listen(listen_fd_.load(), options_.backlog) != 0) {
            raw_close();
            freeaddrinfo(local_addr_info);
            std::cerr << "Failed to listen on socket: " << std::strerror(errno) << std::endl;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        freeaddrinfo(local_addr_info);
    } else { // Client
        addrinfo* remote_addr_info = nullptr;
        if (!config_json.contains("remote")) {
            std::cerr << "TCP Comm config error: missing 'remote' for client role." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("remote"), kTcpSocketType, &remote_addr_info) != HAKO_PDU_ERR_OK) {
            std::cerr << "TCP Comm config error: failed to resolve remote address." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        std::memcpy(&remote_addr_info_, remote_addr_info->ai_addr, remote_addr_info->ai_addrlen);
        remote_addr_len_ = remote_addr_info->ai_addrlen;
        freeaddrinfo(remote_addr_info);
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_close() noexcept {
    raw_stop();
    int current_client_fd = client_fd_.load();
    if (current_client_fd >= 0) {
        ::close(current_client_fd);
        client_fd_ = -1;
    }
    int current_listen_fd = listen_fd_.load();
    if (current_listen_fd >= 0) {
        ::close(current_listen_fd);
        listen_fd_ = -1;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_start() noexcept {
    if (is_running_flag_) {
        std::cerr << "TCP Comm start requested while already running." << std::endl;
        return HAKO_PDU_ERR_BUSY;
    }
    is_running_flag_ = true;
    if (role_ == Role::Server) {
        comm_thread_ = std::thread(&TcpComm::server_loop, this);
    } else {
        comm_thread_ = std::thread(&TcpComm::client_loop, this);
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_stop() noexcept {
    if (!is_running_flag_) {
        return HAKO_PDU_ERR_OK;
    }
    is_running_flag_ = false;

    int current_listen_fd = listen_fd_.load();
    if (current_listen_fd >= 0) {
        ::shutdown(current_listen_fd, SHUT_RD);
        ::close(current_listen_fd);
        listen_fd_ = -1;
    }
    int current_client_fd = client_fd_.load();
    if (current_client_fd >= 0) {
        ::shutdown(current_client_fd, SHUT_RDWR);
    }

    if (comm_thread_.joinable()) {
        comm_thread_.join();
    }
    is_connected_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_is_running(bool& running) noexcept {
    running = is_running_flag_.load() && is_connected_.load();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_send(const std::vector<std::byte>& data) noexcept {
    int current_client_fd = client_fd_.load();
    if (current_client_fd < 0) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return write_data(current_client_fd, data.data(), data.size());
}

void TcpComm::server_loop() {
    while (is_running_flag_) {
        sockaddr_storage client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int accepted_fd = ::accept(listen_fd_.load(), reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (accepted_fd < 0) {
            if (is_running_flag_) {
                std::cerr << "TCP Comm accept failed: " << std::strerror(errno) << std::endl;
            }
            continue; // or break
        }

        client_fd_ = accepted_fd;
        configure_socket_options(client_fd_.load(), options_);
        is_connected_ = true;

        while (is_running_flag_) {
            std::vector<std::byte> header_buf(sizeof(MetaPdu));
            HakoPduErrorType err = read_data(client_fd_.load(), header_buf.data(), header_buf.size());
            if (err != HAKO_PDU_ERR_OK) {
                std::cerr << "TCP Comm read header failed: " << static_cast<int>(err) << std::endl;
                // Connection closed or error
                break;
            }
            
            MetaPdu meta;
            std::memcpy(&meta, header_buf.data(), sizeof(MetaPdu));
            meta.body_len = ntohl(meta.body_len); // Assuming body_len is what we need

            if (meta.body_len > 0) {
                std::vector<std::byte> body_buf(meta.body_len);
                err = read_data(client_fd_.load(), body_buf.data(), body_buf.size());
                if (err != HAKO_PDU_ERR_OK) {
                    std::cerr << "TCP Comm read body failed: " << static_cast<int>(err) << std::endl;
                    // Incomplete packet or error
                    break;
                }
                header_buf.insert(header_buf.end(), body_buf.begin(), body_buf.end());
            }
            
            on_raw_data_received(header_buf);
        }
        is_connected_ = false;
        int current_client_fd = client_fd_.load();
        if (current_client_fd >= 0) {
            ::close(current_client_fd);
            client_fd_ = -1;
        }
    }
}

void TcpComm::client_loop() {
    while (is_running_flag_) {
        client_fd_ = ::socket(remote_addr_info_.ss_family, kTcpSocketType, 0);
        if (client_fd_.load() < 0) {
            std::cerr << "TCP Comm client socket create failed: " << std::strerror(errno) << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        addrinfo remote_info{};
        remote_info.ai_family = remote_addr_info_.ss_family;
        remote_info.ai_addr = reinterpret_cast<sockaddr*>(&remote_addr_info_);
        remote_info.ai_addrlen = remote_addr_len_;

        HakoPduErrorType connect_err = connect_with_timeout(client_fd_.load(), &remote_info, options_);
        if (connect_err != HAKO_PDU_ERR_OK) {
            std::cerr << "TCP Comm connect failed: " << static_cast<int>(connect_err) << std::endl;
            ::close(client_fd_.load());
            client_fd_ = -1;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        configure_socket_options(client_fd_.load(), options_);
        is_connected_ = true;

        while (is_running_flag_) {
            std::vector<std::byte> header_buf(sizeof(MetaPdu));
            HakoPduErrorType err = read_data(client_fd_.load(), header_buf.data(), header_buf.size());
            if (err != HAKO_PDU_ERR_OK) {
                std::cerr << "TCP Comm read header failed: " << static_cast<int>(err) << std::endl;
                break; // Disconnected
            }

            MetaPdu meta;
            std::memcpy(&meta, header_buf.data(), sizeof(MetaPdu));
            meta.body_len = ntohl(meta.body_len);

            std::vector<std::byte> body_buf;
            if (meta.body_len > 0) {
                body_buf.resize(meta.body_len);
                err = read_data(client_fd_.load(), body_buf.data(), body_buf.size());
                if (err != HAKO_PDU_ERR_OK) {
                    std::cerr << "TCP Comm read body failed: " << static_cast<int>(err) << std::endl;
                    break; // Incomplete packet
                }
            }

            std::vector<std::byte> full_packet = header_buf;
            full_packet.insert(full_packet.end(), body_buf.begin(), body_buf.end());
            on_raw_data_received(full_packet);
        }
        ::close(client_fd_.load());
        client_fd_ = -1;
    }
}

HakoPduErrorType TcpComm::read_data(int fd, std::byte* buffer, size_t size) noexcept {
    size_t total_received = 0;
    while (total_received < size) {
        ssize_t received = ::recv(fd, buffer + total_received, size - total_received, 0);
        if (received > 0) {
            total_received += received;
        } else if (received == 0) {
            return HAKO_PDU_ERR_IO_ERROR; // Connection closed
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Should use poll/select for non-blocking
                continue;
            }
            return map_errno_to_error(errno);
        }
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::write_data(int fd, const std::byte* buffer, size_t size) noexcept {
    size_t total_sent = 0;
    while (total_sent < size) {
        ssize_t sent = ::send(fd, buffer + total_sent, size - total_sent, 0);
        if (sent > 0) {
            total_sent += sent;
        } else if (sent == 0) {
            return HAKO_PDU_ERR_IO_ERROR; // Should not happen
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Should use poll/select for non-blocking
                continue;
            }
            return map_errno_to_error(errno);
        }
    }
    return HAKO_PDU_ERR_OK;
}


// Configuration helpers
// ... (Copied from old tcp_endpoint.cpp)
HakoPduErrorType TcpComm::configure_socket_options(int fd, const Options& options) noexcept
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

HakoPduErrorType TcpComm::configure_timeouts(int fd, const Options& options) noexcept
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

HakoPduErrorType TcpComm::connect_with_timeout(int fd, addrinfo* remote_addr, const Options& options) noexcept
{
    if (!remote_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return HAKO_PDU_ERR_IO_ERROR;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return HAKO_PDU_ERR_IO_ERROR;

    int connect_result = ::connect(fd, remote_addr->ai_addr, remote_addr->ai_addrlen);
    if (connect_result == 0) {
        fcntl(fd, F_SETFL, flags); // Restore flags
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
    fcntl(fd, F_SETFL, flags); // Restore flags
    return HAKO_PDU_ERR_OK;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
