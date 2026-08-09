#include "hakoniwa/pdu/comm/comm_tcp.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include "tcp_diagnostics.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <iostream>
#include <array>
#include <cstddef>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
constexpr int kTcpSocketType = SOCK_STREAM;
constexpr uint32_t kMaxV1PacketSize = 4 * 1024 * 1024;

uint32_t read_le32(const std::byte* data) noexcept
{
    return static_cast<uint32_t>(std::to_integer<unsigned char>(data[0]))
        | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[1])) << 8)
        | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[2])) << 16)
        | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[3])) << 24);
}

uint32_t bswap32(uint32_t value) noexcept
{
    return ((value & 0x000000FFu) << 24)
        | ((value & 0x0000FF00u) << 8)
        | ((value & 0x00FF0000u) >> 8)
        | ((value & 0xFF000000u) >> 24);
}

uint32_t from_le32(uint32_t value) noexcept
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return bswap32(value);
#else
    return value;
#endif
}
}

TcpComm::TcpComm() {}
TcpComm::~TcpComm() {
    raw_close();
}

HakoPduErrorType TcpComm::raw_open(const std::string& config_path) {
    if (is_valid_socket(client_fd_.load()) || is_valid_socket(listen_fd_.load())) {
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
    comm_name_ = config_json.value("name", std::string{"tcp"});
    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());

    if (config_json.contains("comm_raw_version")) {
        if (!config_json.at("comm_raw_version").is_string()) {
            std::cerr << "TCP Comm config error: 'comm_raw_version' must be a string." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        const std::string version = config_json.at("comm_raw_version").get<std::string>();
        if (!set_packet_version(version)) {
            std::cerr << "TCP Comm config error: unsupported comm_raw_version '" << version << "'." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }
    
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

    NameResolverConfig name_resolver{};
    std::string name_resolver_error;
    auto resolver_ret = load_name_resolver_config(config_json, config_path, name_resolver, name_resolver_error);
    if (resolver_ret != HAKO_PDU_ERR_OK) {
        std::cerr << "TCP Comm config error: " << name_resolver_error << std::endl;
        return resolver_ret;
    }

    if (role_ == Role::Server) {
        AddressInfo* local_addr_info = nullptr;
        if (!config_json.contains("local")) {
            std::cerr << "TCP Comm config error: missing 'local' for server role." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("local"), kTcpSocketType, &local_addr_info, &name_resolver) != HAKO_PDU_ERR_OK) {
            std::cerr << "TCP Comm config error: failed to resolve local address." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }

        listen_fd_ = create_socket(local_addr_info->ai_family, local_addr_info->ai_socktype, local_addr_info->ai_protocol);
        if (!is_valid_socket(listen_fd_.load())) {
            const int error_number = last_socket_error();
            free_address_info(local_addr_info);
            log_tcp_socket_failure(
                {"tcp", comm_name_, "server", 0, {}},
                "create_socket",
                error_number,
                map_errno_to_error(error_number),
                0,
                stopping_.load());
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (configure_socket_options(listen_fd_.load(), options_) != HAKO_PDU_ERR_OK) {
            log_tcp_mapped_failure(
                {"tcp", comm_name_, "server", 0, {}},
                "configure_listener",
                HAKO_PDU_ERR_IO_ERROR,
                0,
                "failed to configure socket options",
                stopping_.load());
            raw_close();
            free_address_info(local_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        SocketLength local_addr_len = 0;
        if (!to_socket_length(local_addr_info->ai_addrlen, local_addr_len)) {
            raw_close();
            free_address_info(local_addr_info);
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (bind_socket(listen_fd_.load(), local_addr_info->ai_addr, local_addr_len) != 0) {
            const int error_number = last_socket_error();
            log_tcp_socket_failure(
                {"tcp", comm_name_, "server", 0, {}},
                "bind",
                error_number,
                map_errno_to_error(error_number),
                0,
                stopping_.load());
            raw_close();
            free_address_info(local_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        if (listen_socket(listen_fd_.load(), options_.backlog) != 0) {
            const int error_number = last_socket_error();
            log_tcp_socket_failure(
                {"tcp", comm_name_, "server", 0, {}},
                "listen",
                error_number,
                map_errno_to_error(error_number),
                0,
                stopping_.load());
            raw_close();
            free_address_info(local_addr_info);
            return HAKO_PDU_ERR_IO_ERROR;
        }
        free_address_info(local_addr_info);
    } else { // Client
        AddressInfo* remote_addr_info = nullptr;
        if (!config_json.contains("remote")) {
            std::cerr << "TCP Comm config error: missing 'remote' for client role." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        if (resolve_address(config_json.at("remote"), kTcpSocketType, &remote_addr_info, &name_resolver) != HAKO_PDU_ERR_OK) {
            std::cerr << "TCP Comm config error: failed to resolve remote address." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        std::memcpy(&remote_addr_info_, remote_addr_info->ai_addr, remote_addr_info->ai_addrlen);
        if (!to_socket_length(remote_addr_info->ai_addrlen, remote_addr_len_)) {
            free_address_info(remote_addr_info);
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        peer_endpoint_ = format_socket_address(
            reinterpret_cast<SocketAddress*>(&remote_addr_info_), remote_addr_len_);
        free_address_info(remote_addr_info);
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_close() noexcept {
    raw_stop();
    SocketHandle current_client_fd = client_fd_.load();
    if (is_valid_socket(current_client_fd)) {
        (void)close_socket(current_client_fd);
        client_fd_ = kInvalidSocket;
    }
    SocketHandle current_listen_fd = listen_fd_.load();
    if (is_valid_socket(current_listen_fd)) {
        (void)close_socket(current_listen_fd);
        listen_fd_ = kInvalidSocket;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_start() noexcept {
    if (is_running_flag_) {
        std::cerr << "TCP Comm start requested while already running." << std::endl;
        return HAKO_PDU_ERR_BUSY;
    }
    stopping_ = false;
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
    stopping_ = true;
    is_running_flag_ = false;

    SocketHandle current_listen_fd = listen_fd_.load();
    if (is_valid_socket(current_listen_fd)) {
        shutdown_socket(current_listen_fd, SocketShutdownMode::Read);
        (void)close_socket(current_listen_fd);
        listen_fd_ = kInvalidSocket;
    }
    SocketHandle current_client_fd = client_fd_.exchange(kInvalidSocket);
    if (is_valid_socket(current_client_fd)) {
        shutdown_socket(current_client_fd, SocketShutdownMode::ReadWrite);
        (void)close_socket(current_client_fd);
    }

    if (comm_thread_.joinable()) {
        comm_thread_.join();
    }
    is_connected_ = false;
    disconnect_notified_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_is_running(bool& running) noexcept {
    running = is_running_flag_.load() && is_connected_.load();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::raw_send(const std::vector<std::byte>& data) noexcept {
    #ifdef ENABLE_DEBUG_MESSAGES
    std::cout << "DEBUG: TCP Comm raw_send called with " << data.size() << " bytes." << std::endl;
    #endif
    SocketHandle current_client_fd = client_fd_.load();
    if (!is_valid_socket(current_client_fd)) {
        std::cout << "TCP Comm send failed: not connected." << std::endl;
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        std::cerr << "TCP Comm send failed: endpoint configured as IN only." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    #ifdef ENABLE_DEBUG_MESSAGES
    std::cout << "DEBUG: TCP Comm sending " << data.size() << " bytes." << std::endl;
    #endif
    HakoPduErrorType err = write_data(current_client_fd, data.data(), data.size(), "send_packet");
    if (err != HAKO_PDU_ERR_OK) {
        notify_disconnect_if_needed_(err, "send");
        close_failed_connection_(current_client_fd);
    }
    return err;
}

void TcpComm::server_loop() {
    while (is_running_flag_) {
        SocketAddressStorage client_addr{};
        SocketLength client_len = sizeof(client_addr);
        SocketHandle accepted_fd = accept_socket(listen_fd_.load(), reinterpret_cast<SocketAddress*>(&client_addr), &client_len);

        if (!is_valid_socket(accepted_fd)) {
            if (is_running_flag_) {
                const int error_number = last_socket_error();
                log_tcp_socket_failure(
                    {"tcp", comm_name_, "server", connection_id_.load(), peer_endpoint_},
                    "accept",
                    error_number,
                    map_errno_to_error(error_number),
                    options_.read_timeout_ms,
                    stopping_.load());
            }
            continue; // or break
        }

        client_fd_ = accepted_fd;
        connection_id_ = next_tcp_connection_id();
        peer_endpoint_ = format_socket_address(
            reinterpret_cast<SocketAddress*>(&client_addr), client_len);
        if (configure_socket_options(client_fd_.load(), options_) != HAKO_PDU_ERR_OK) {
            log_tcp_mapped_failure(
                {"tcp", comm_name_, "server", connection_id_.load(), peer_endpoint_},
                "configure_connection",
                HAKO_PDU_ERR_IO_ERROR,
                0,
                "failed to configure accepted socket",
                stopping_.load());
            close_failed_connection_(accepted_fd);
            continue;
        }
        is_connected_ = true;
        disconnect_notified_ = false;

        while (is_running_flag_) {
            if (packet_version() == "v1") {
                std::array<std::byte, 4> header_len_buf{};
                HakoPduErrorType err = read_data(client_fd_.load(), header_len_buf.data(), header_len_buf.size(), "recv_v1_header_length");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "server read v1 header");
                    break;
                }
                uint32_t header_len = read_le32(header_len_buf.data());
                if (header_len == 0 || header_len > kMaxV1PacketSize) {
                    log_tcp_protocol_error(
                        {"tcp", comm_name_, "server", connection_id_.load(), peer_endpoint_},
                        "decode_v1_header_length",
                        "invalid header length " + std::to_string(header_len),
                        stopping_.load());
                    notify_disconnect_if_needed_(HAKO_PDU_ERR_IO_ERROR, "server invalid v1 header length");
                    break;
                }
                std::vector<std::byte> packet_buf(4 + header_len);
                std::memcpy(packet_buf.data(), header_len_buf.data(), header_len_buf.size());
                err = read_data(client_fd_.load(), packet_buf.data() + 4, header_len, "recv_v1_payload");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "server read v1 payload");
                    break;
                }
                on_raw_data_received(packet_buf);
                continue;
            }

            std::vector<std::byte> header_buf(sizeof(MetaPdu));
            HakoPduErrorType err = read_data(client_fd_.load(), header_buf.data(), header_buf.size(), "recv_header");
            if (err != HAKO_PDU_ERR_OK) {
                notify_disconnect_if_needed_(err, "server read header");
                break;
            }
            #ifdef ENABLE_DEBUG_MESSAGES
            std::cout << "DEBUG: TCP Comm server received header." << std::endl;
            #endif
            MetaPdu meta;
            std::memcpy(&meta, header_buf.data(), sizeof(MetaPdu));
            meta.body_len = from_le32(meta.body_len);

            if (meta.body_len > 0) {
                std::vector<std::byte> body_buf(meta.body_len);
                err = read_data(client_fd_.load(), body_buf.data(), body_buf.size(), "recv_body");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "server read body");
                    break;
                }
                header_buf.insert(header_buf.end(), body_buf.begin(), body_buf.end());
            }
            #ifdef ENABLE_DEBUG_MESSAGES
            std::cout << "DEBUG: TCP Comm server received full packet." << std::endl;
            #endif
            on_raw_data_received(header_buf);
        }
        is_connected_ = false;
        SocketHandle current_client_fd = client_fd_.load();
        if (is_valid_socket(current_client_fd)) {
            (void)close_socket(current_client_fd);
            client_fd_ = kInvalidSocket;
        }
    }
}

void TcpComm::client_loop() {
    while (is_running_flag_) {
        connection_id_ = next_tcp_connection_id();
        client_fd_ = create_socket(remote_addr_info_.ss_family, kTcpSocketType, 0);
        if (!is_valid_socket(client_fd_.load())) {
            const int error_number = last_socket_error();
            log_tcp_socket_failure(
                {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
                "create_socket",
                error_number,
                map_errno_to_error(error_number),
                0,
                stopping_.load());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        AddressInfo remote_info{};
        remote_info.ai_family = remote_addr_info_.ss_family;
        remote_info.ai_addr = reinterpret_cast<SocketAddress*>(&remote_addr_info_);
        remote_info.ai_addrlen = remote_addr_len_;

        HakoPduErrorType connect_err = connect_with_timeout(client_fd_.load(), &remote_info, options_);
        if (connect_err != HAKO_PDU_ERR_OK) {
            (void)close_socket(client_fd_.load());
            client_fd_ = kInvalidSocket;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (configure_socket_options(client_fd_.load(), options_) != HAKO_PDU_ERR_OK) {
            log_tcp_mapped_failure(
                {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
                "configure_connection",
                HAKO_PDU_ERR_IO_ERROR,
                0,
                "failed to configure connected socket",
                stopping_.load());
            close_failed_connection_(client_fd_.load());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        peer_endpoint_ = format_socket_address(
            reinterpret_cast<SocketAddress*>(&remote_addr_info_), remote_addr_len_);
        is_connected_ = true;
        disconnect_notified_ = false;

        while (is_running_flag_) {
            if (packet_version() == "v1") {
                std::array<std::byte, 4> header_len_buf{};
                HakoPduErrorType err = read_data(client_fd_.load(), header_len_buf.data(), header_len_buf.size(), "recv_v1_header_length");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "client read v1 header");
                    break;
                }
                uint32_t header_len = read_le32(header_len_buf.data());
                if (header_len == 0 || header_len > kMaxV1PacketSize) {
                    log_tcp_protocol_error(
                        {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
                        "decode_v1_header_length",
                        "invalid header length " + std::to_string(header_len),
                        stopping_.load());
                    notify_disconnect_if_needed_(HAKO_PDU_ERR_IO_ERROR, "client invalid v1 header length");
                    break;
                }
                std::vector<std::byte> packet_buf(4 + header_len);
                std::memcpy(packet_buf.data(), header_len_buf.data(), header_len_buf.size());
                err = read_data(client_fd_.load(), packet_buf.data() + 4, header_len, "recv_v1_payload");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "client read v1 payload");
                    break;
                }
                on_raw_data_received(packet_buf);
                continue;
            }

            std::vector<std::byte> header_buf(sizeof(MetaPdu));
            HakoPduErrorType err = read_data(client_fd_.load(), header_buf.data(), header_buf.size(), "recv_header");
            if (err != HAKO_PDU_ERR_OK) {
                notify_disconnect_if_needed_(err, "client read header");
                break; // Disconnected
            }
            #ifdef ENABLE_DEBUG_MESSAGES
            std::cout << "DEBUG: TCP Comm client received header." << std::endl;
            #endif
            MetaPdu meta;
            std::memcpy(&meta, header_buf.data(), sizeof(MetaPdu));
            meta.body_len = from_le32(meta.body_len);

            std::vector<std::byte> body_buf;
            if (meta.body_len > 0) {
                body_buf.resize(meta.body_len);
                err = read_data(client_fd_.load(), body_buf.data(), body_buf.size(), "recv_body");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "client read body");
                    break; // Incomplete packet
                }
            }

            std::vector<std::byte> full_packet = header_buf;
            full_packet.insert(full_packet.end(), body_buf.begin(), body_buf.end());
            on_raw_data_received(full_packet);
        }
        (void)close_socket(client_fd_.load());
        client_fd_ = kInvalidSocket;
    }
}

void TcpComm::notify_disconnect_if_needed_(HakoPduErrorType reason, const char* context) noexcept
{
    if (stopping_.load() || !is_connected_.load()) {
        return;
    }
    bool expected = false;
    if (!disconnect_notified_.compare_exchange_strong(expected, true)) {
        return;
    }
    is_connected_ = false;
    notify_disconnected_(static_cast<int>(reason), std::string("tcp disconnected: ") + context);
}

HakoPduErrorType TcpComm::read_data(SocketHandle fd, std::byte* buffer, size_t size, const char* operation) noexcept {
    size_t total_received = 0;
    while (total_received < size) {
        SocketSize received = recv_socket(fd, buffer + total_received, size - total_received, 0);
        if (received > 0) {
            total_received += received;
        } else if (received == 0) {
            log_tcp_peer_closed(
                {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                operation,
                stopping_.load());
            return HAKO_PDU_ERR_IO_ERROR; // Connection closed
        } else {
            const int error_number = last_socket_error();
            if (is_socket_timeout(error_number)) {
                const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                log_tcp_socket_failure(
                    {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                    operation,
                    error_number,
                    mapped_error,
                    options_.read_timeout_ms,
                    stopping_.load());
                return mapped_error;
            }
            if (is_socket_would_block(error_number)) {
                if (options_.blocking && options_.read_timeout_ms > 0) {
                    const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                    log_tcp_socket_failure(
                        {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                        operation,
                        error_number,
                        mapped_error,
                        options_.read_timeout_ms,
                        stopping_.load());
                    return mapped_error;
                }
                continue;
            }
            const auto mapped_error = map_errno_to_error(error_number);
            log_tcp_socket_failure(
                {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                operation,
                error_number,
                mapped_error,
                options_.read_timeout_ms,
                stopping_.load());
            return mapped_error;
        }
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::write_data(SocketHandle fd, const std::byte* buffer, size_t size, const char* operation) noexcept {
    size_t total_sent = 0;
    while (total_sent < size) {
        SocketSize sent = send_socket(fd, buffer + total_sent, size - total_sent, 0);
        if (sent > 0) {
            total_sent += sent;
        } else if (sent == 0) {
            log_tcp_protocol_error(
                {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                operation,
                "send returned zero bytes",
                stopping_.load());
            return HAKO_PDU_ERR_IO_ERROR; // Should not happen
        } else {
            const int error_number = last_socket_error();
            if (is_socket_timeout(error_number)) {
                const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                log_tcp_socket_failure(
                    {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                    operation,
                    error_number,
                    mapped_error,
                    options_.write_timeout_ms,
                    stopping_.load());
                return mapped_error;
            }
            if (is_socket_would_block(error_number)) {
                if (options_.blocking && options_.write_timeout_ms > 0) {
                    const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                    log_tcp_socket_failure(
                        {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                        operation,
                        error_number,
                        mapped_error,
                        options_.write_timeout_ms,
                        stopping_.load());
                    return mapped_error;
                }
                continue;
            }
            const auto mapped_error = map_errno_to_error(error_number);
            log_tcp_socket_failure(
                {"tcp", comm_name_, role_ == Role::Server ? "server" : "client", connection_id_.load(), peer_endpoint_},
                operation,
                error_number,
                mapped_error,
                options_.write_timeout_ms,
                stopping_.load());
            return mapped_error;
        }
    }
    #ifdef ENABLE_DEBUG_MESSAGES
    std::cout << "DEBUG: TCP Comm sent " << total_sent << " bytes." << std::endl;
    #endif
    return HAKO_PDU_ERR_OK;
}

void TcpComm::close_failed_connection_(SocketHandle expected_fd) noexcept
{
    SocketHandle current_fd = expected_fd;
    if (client_fd_.compare_exchange_strong(current_fd, kInvalidSocket)) {
        shutdown_socket(expected_fd, SocketShutdownMode::ReadWrite);
        (void)close_socket(expected_fd);
    }
}


// Configuration helpers
// ... (Copied from old tcp_endpoint.cpp)
HakoPduErrorType TcpComm::configure_socket_options(SocketHandle fd, const Options& options) noexcept
{
    if (options.reuse_address) {
        if (set_socket_option_int(fd, SOL_SOCKET, SO_REUSEADDR, 1) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.keepalive) {
        if (set_socket_option_int(fd, SOL_SOCKET, SO_KEEPALIVE, 1) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.no_delay) {
        if (set_socket_option_int(fd, IPPROTO_TCP, TCP_NODELAY, 1) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.recv_buffer_size > 0) {
        if (set_socket_option_int(fd, SOL_SOCKET, SO_RCVBUF, options.recv_buffer_size) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.send_buffer_size > 0) {
        if (set_socket_option_int(fd, SOL_SOCKET, SO_SNDBUF, options.send_buffer_size) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.linger_enabled) {
        if (set_socket_linger_option(fd, true, options.linger_timeout_sec) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    return configure_timeouts(fd, options);
}

HakoPduErrorType TcpComm::configure_timeouts(SocketHandle fd, const Options& options) noexcept
{
    if (options.read_timeout_ms >= 0) {
        if (set_socket_timeout_option(fd, SO_RCVTIMEO, options.read_timeout_ms) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (options.write_timeout_ms >= 0) {
        if (set_socket_timeout_option(fd, SO_SNDTIMEO, options.write_timeout_ms) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    if (!options.blocking) {
        if (set_socket_nonblocking(fd, true) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpComm::connect_with_timeout(SocketHandle fd, AddressInfo* remote_addr, const Options& options) noexcept
{
    if (!remote_addr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const int original_flags = get_socket_status_flags(fd);
    if (original_flags < 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (set_socket_nonblocking(fd, true) != HAKO_PDU_ERR_OK) return HAKO_PDU_ERR_IO_ERROR;

    SocketLength remote_addr_len = 0;
    if (!to_socket_length(remote_addr->ai_addrlen, remote_addr_len)) {
        (void)set_socket_status_flags(fd, original_flags);
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    int connect_result = connect_socket(fd, remote_addr->ai_addr, remote_addr_len);
    if (connect_result == 0) {
        (void)set_socket_status_flags(fd, original_flags);
        return HAKO_PDU_ERR_OK;
    }
    const int connect_error = last_socket_error();
    if (!is_socket_connect_in_progress(connect_error)) {
        log_tcp_socket_failure(
            {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
            "connect",
            connect_error,
            map_errno_to_error(connect_error),
            options.connect_timeout_ms,
            stopping_.load());
        (void)set_socket_status_flags(fd, original_flags);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    bool ready = false;
    HakoPduErrorType wait_err = wait_socket(fd, SocketWaitCondition::Writable, options.connect_timeout_ms, ready);
    if (wait_err != HAKO_PDU_ERR_OK || !ready) {
        log_tcp_mapped_failure(
            {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
            "connect_wait",
            wait_err != HAKO_PDU_ERR_OK ? wait_err : HAKO_PDU_ERR_TIMEOUT,
            options.connect_timeout_ms,
            "socket did not become writable",
            stopping_.load());
        (void)set_socket_status_flags(fd, original_flags);
        return wait_err != HAKO_PDU_ERR_OK ? wait_err : HAKO_PDU_ERR_TIMEOUT;
    }
    int so_error = 0;
    if (get_socket_option_int(fd, SOL_SOCKET, SO_ERROR, so_error) != HAKO_PDU_ERR_OK) {
        const int error_number = last_socket_error();
        log_tcp_socket_failure(
            {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
            "connect_get_so_error",
            error_number,
            map_errno_to_error(error_number),
            options.connect_timeout_ms,
            stopping_.load());
        (void)set_socket_status_flags(fd, original_flags);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (so_error != 0) {
        log_tcp_socket_failure(
            {"tcp", comm_name_, "client", connection_id_.load(), peer_endpoint_},
            "connect_complete",
            so_error,
            map_errno_to_error(so_error),
            options.connect_timeout_ms,
            stopping_.load());
        (void)set_socket_status_flags(fd, original_flags);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    (void)set_socket_status_flags(fd, original_flags);
    return HAKO_PDU_ERR_OK;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
