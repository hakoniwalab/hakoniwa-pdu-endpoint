#include "hakoniwa/pdu/comm/comm_tcp_mux.hpp"
#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include "tcp_diagnostics.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <array>
#include <iostream>
#include <vector>

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

class TcpSessionComm final : public PduCommRaw
{
public:
    TcpSessionComm(SocketHandle fd, std::uint64_t connection_id, std::string peer)
        : fd_(fd), connection_id_(connection_id), peer_endpoint_(std::move(peer)) {}
    ~TcpSessionComm() override { (void)raw_close(); }

protected:
    HakoPduErrorType raw_open(const std::string& config_path) override
    {
        if (!is_valid_socket(fd_.load())) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        std::ifstream config_stream(config_path);
        if (!config_stream) {
            std::cerr << "Failed to open TCP Mux Comm config file: " << config_path << std::endl;
            return HAKO_PDU_ERR_IO_ERROR;
        }

        nlohmann::json config_json;
        try {
            config_stream >> config_json;
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "TCP Mux Comm config JSON parse error: " << e.what() << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }

        if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "tcp") {
            std::cerr << "TCP Mux Comm config error: protocol is not 'tcp'." << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        comm_name_ = config_json.value("name", std::string{"tcp_mux_session"});
        if (config_json.contains("direction")) {
            config_direction_ = parse_direction(config_json.at("direction").get<std::string>());
        }

        if (config_json.contains("comm_raw_version")) {
            if (!config_json.at("comm_raw_version").is_string()) {
                std::cerr << "TCP Mux Comm config error: 'comm_raw_version' must be a string." << std::endl;
                return HAKO_PDU_ERR_INVALID_ARGUMENT;
            }
            const std::string version = config_json.at("comm_raw_version").get<std::string>();
            if (!set_packet_version(version)) {
                std::cerr << "TCP Mux Comm config error: unsupported comm_raw_version '" << version << "'." << std::endl;
                return HAKO_PDU_ERR_INVALID_ARGUMENT;
            }
        }

        if (config_json.contains("options")) {
            const auto& opts = config_json.at("options");
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

        return configure_socket_options_(fd_.load(), options_);
    }

    HakoPduErrorType raw_close() noexcept override
    {
        raw_stop();
        const SocketHandle current_fd = fd_.exchange(kInvalidSocket);
        if (is_valid_socket(current_fd)) {
            (void)close_socket(current_fd);
        }
        return HAKO_PDU_ERR_OK;
    }

    HakoPduErrorType raw_start() noexcept override
    {
        if (is_running_) {
            return HAKO_PDU_ERR_BUSY;
        }
        stopping_ = false;
        is_running_ = true;
        disconnect_notified_ = false;
        recv_thread_ = std::thread(&TcpSessionComm::recv_loop_, this);
        return HAKO_PDU_ERR_OK;
    }

    HakoPduErrorType raw_stop() noexcept override
    {
        if (!is_running_) {
            if (recv_thread_.joinable()) {
                recv_thread_.join();
            }
            return HAKO_PDU_ERR_OK;
        }
        stopping_ = true;
        is_running_ = false;
        const SocketHandle current_fd = fd_.exchange(kInvalidSocket);
        if (is_valid_socket(current_fd)) {
            shutdown_socket(current_fd, SocketShutdownMode::ReadWrite);
            (void)close_socket(current_fd);
        }
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
        disconnect_notified_ = false;
        return HAKO_PDU_ERR_OK;
    }

    HakoPduErrorType raw_is_running(bool& running) noexcept override
    {
        running = is_running_.load();
        return HAKO_PDU_ERR_OK;
    }

    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override
    {
        const SocketHandle current_fd = fd_.load();
        if (!is_valid_socket(current_fd)) {
            return HAKO_PDU_ERR_NOT_RUNNING;
        }
        if (config_direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
        const auto result = write_data_(current_fd, data.data(), data.size(), "send_packet");
        if (result != HAKO_PDU_ERR_OK) {
            notify_disconnect_if_needed_(result, "mux session send");
            close_failed_connection_(current_fd);
        }
        return result;
    }

private:
    struct Options {
        int read_timeout_ms = 0;
        int write_timeout_ms = 0;
        bool blocking = true;
        bool reuse_address = true;
        bool keepalive = true;
        bool no_delay = true;
        int recv_buffer_size = 8192;
        int send_buffer_size = 8192;
        bool linger_enabled = false;
        int linger_timeout_sec = 0;
    };

    HakoPduErrorType configure_socket_options_(SocketHandle fd, const Options& options) noexcept
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

    HakoPduErrorType read_data_(SocketHandle fd, std::byte* buffer, size_t size, const char* operation) noexcept
    {
        size_t total_received = 0;
        while (total_received < size) {
            SocketSize received = recv_socket(fd, buffer + total_received, size - total_received, 0);
            if (received > 0) {
                total_received += received;
            } else if (received == 0) {
                log_tcp_peer_closed(
                    {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
                    operation,
                    stopping_.load());
                return HAKO_PDU_ERR_IO_ERROR;
            } else {
                const int error_number = last_socket_error();
                if (is_socket_timeout(error_number)) {
                    const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                    log_tcp_socket_failure(
                        {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
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
                            {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
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
                    {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
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

    HakoPduErrorType write_data_(SocketHandle fd, const std::byte* buffer, size_t size, const char* operation) noexcept
    {
        size_t total_sent = 0;
        while (total_sent < size) {
            SocketSize sent = send_socket(fd, buffer + total_sent, size - total_sent, 0);
            if (sent > 0) {
                total_sent += sent;
            } else if (sent == 0) {
                log_tcp_protocol_error(
                    {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
                    operation,
                    "send returned zero bytes",
                    stopping_.load());
                return HAKO_PDU_ERR_IO_ERROR;
            } else {
                const int error_number = last_socket_error();
                if (is_socket_timeout(error_number)) {
                    const auto mapped_error = HAKO_PDU_ERR_TIMEOUT;
                    log_tcp_socket_failure(
                        {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
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
                            {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
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
                    {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
                    operation,
                    error_number,
                    mapped_error,
                    options_.write_timeout_ms,
                    stopping_.load());
                return mapped_error;
            }
        }
        return HAKO_PDU_ERR_OK;
    }

    void recv_loop_()
    {
        while (is_running_) {
            if (packet_version() == "v1") {
                std::array<std::byte, 4> header_len_buf{};
                const SocketHandle current_fd = fd_.load();
                HakoPduErrorType err = read_data_(current_fd, header_len_buf.data(), header_len_buf.size(), "recv_v1_header_length");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "mux session read v1 header");
                    break;
                }
                uint32_t header_len = read_le32(header_len_buf.data());
                if (header_len == 0 || header_len > kMaxV1PacketSize) {
                    log_tcp_protocol_error(
                        {"tcp_mux", comm_name_, "session", connection_id_, peer_endpoint_},
                        "decode_v1_header_length",
                        "invalid header length " + std::to_string(header_len),
                        stopping_.load());
                    notify_disconnect_if_needed_(HAKO_PDU_ERR_IO_ERROR, "mux session invalid v1 header length");
                    break;
                }
                std::vector<std::byte> packet_buf(4 + header_len);
                std::memcpy(packet_buf.data(), header_len_buf.data(), header_len_buf.size());
                err = read_data_(current_fd, packet_buf.data() + 4, header_len, "recv_v1_payload");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "mux session read v1 payload");
                    break;
                }
                on_raw_data_received(packet_buf);
                continue;
            }

            std::vector<std::byte> header_buf(sizeof(MetaPdu));
            const SocketHandle current_fd = fd_.load();
            HakoPduErrorType err = read_data_(current_fd, header_buf.data(), header_buf.size(), "recv_header");
            if (err != HAKO_PDU_ERR_OK) {
                notify_disconnect_if_needed_(err, "mux session read header");
                break;
            }
            MetaPdu meta;
            std::memcpy(&meta, header_buf.data(), sizeof(MetaPdu));
            meta.body_len = from_le32(meta.body_len);

            if (meta.body_len > 0) {
                std::vector<std::byte> body_buf(meta.body_len);
                err = read_data_(current_fd, body_buf.data(), body_buf.size(), "recv_body");
                if (err != HAKO_PDU_ERR_OK) {
                    notify_disconnect_if_needed_(err, "mux session read body");
                    break;
                }
                header_buf.insert(header_buf.end(), body_buf.begin(), body_buf.end());
            }
            on_raw_data_received(header_buf);
        }
        is_running_ = false;
        close_failed_connection_(fd_.load());
    }
    void notify_disconnect_if_needed_(HakoPduErrorType reason, const char* context) noexcept
    {
        if (stopping_.load()) {
            return;
        }
        bool expected = false;
        if (!disconnect_notified_.compare_exchange_strong(expected, true)) {
            return;
        }
        notify_disconnected_(static_cast<int>(reason), std::string(context));
    }

    void close_failed_connection_(SocketHandle expected_fd) noexcept
    {
        if (!is_valid_socket(expected_fd)) {
            return;
        }
        SocketHandle current_fd = expected_fd;
        if (fd_.compare_exchange_strong(current_fd, kInvalidSocket)) {
            shutdown_socket(expected_fd, SocketShutdownMode::ReadWrite);
            (void)close_socket(expected_fd);
        }
    }

    std::atomic<SocketHandle> fd_{kInvalidSocket};
    std::atomic<bool> is_running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> disconnect_notified_{false};
    std::thread recv_thread_;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
    Options options_{};
    std::string comm_name_{"tcp_mux_session"};
    std::uint64_t connection_id_ = 0;
    std::string peer_endpoint_;
};

} // namespace

TcpCommMultiplexer::TcpCommMultiplexer() {}
TcpCommMultiplexer::~TcpCommMultiplexer() { close(); }

HakoPduErrorType TcpCommMultiplexer::open(const std::string& config_path)
{
    if (listen_fd_.load() != -1) {
        return HAKO_PDU_ERR_BUSY;
    }

    std::ifstream config_stream(config_path);
    if (!config_stream) {
        std::cerr << "Failed to open TCP Mux config file: " << config_path << std::endl;
        return HAKO_PDU_ERR_IO_ERROR;
    }

    nlohmann::json config_json;
    try {
        config_stream >> config_json;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "TCP Mux config JSON parse error: " << e.what() << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (!config_json.contains("protocol") || config_json.at("protocol").get<std::string>() != "tcp") {
        std::cerr << "TCP Mux config error: protocol is not 'tcp'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("local")) {
        std::cerr << "TCP Mux config error: missing 'local'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!config_json.contains("expected_clients")) {
        std::cerr << "TCP Mux config error: missing 'expected_clients'." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    comm_name_ = config_json.value("name", std::string{"tcp_mux"});

    expected_clients_ = config_json.at("expected_clients").get<size_t>();

    if (config_json.contains("options")) {
        const auto& opts = config_json.at("options");
        options_.backlog = opts.value("backlog", options_.backlog);
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
        std::cerr << "TCP Mux config error: " << name_resolver_error << std::endl;
        return resolver_ret;
    }

    AddressInfo* local_addr_info = nullptr;
    if (resolve_address(config_json.at("local"), kTcpSocketType, &local_addr_info, &name_resolver) != HAKO_PDU_ERR_OK) {
        std::cerr << "TCP Mux config error: failed to resolve local address." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    listen_fd_ = create_socket(local_addr_info->ai_family, local_addr_info->ai_socktype, local_addr_info->ai_protocol);
    if (!is_valid_socket(listen_fd_.load())) {
        const int error_number = last_socket_error();
        free_address_info(local_addr_info);
        log_tcp_socket_failure(
            {"tcp_mux", comm_name_, "listener", 0, {}},
            "create_socket",
            error_number,
            map_errno_to_error(error_number),
            0,
            false);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (configure_socket_options_(listen_fd_.load(), options_) != HAKO_PDU_ERR_OK) {
        log_tcp_mapped_failure(
            {"tcp_mux", comm_name_, "listener", 0, {}},
            "configure_listener",
            HAKO_PDU_ERR_IO_ERROR,
            0,
            "failed to configure socket options",
            false);
        close();
        free_address_info(local_addr_info);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    SocketLength local_addr_len = 0;
    if (!to_socket_length(local_addr_info->ai_addrlen, local_addr_len)) {
        close();
        free_address_info(local_addr_info);
        std::cerr << "Failed to bind socket: local address length overflow." << std::endl;
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (bind_socket(listen_fd_.load(), local_addr_info->ai_addr, local_addr_len) != 0) {
        const int error_number = last_socket_error();
        log_tcp_socket_failure(
            {"tcp_mux", comm_name_, "listener", 0, {}},
            "bind",
            error_number,
            map_errno_to_error(error_number),
            0,
            false);
        close();
        free_address_info(local_addr_info);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (listen_socket(listen_fd_.load(), options_.backlog) != 0) {
        const int error_number = last_socket_error();
        log_tcp_socket_failure(
            {"tcp_mux", comm_name_, "listener", 0, {}},
            "listen",
            error_number,
            map_errno_to_error(error_number),
            0,
            false);
        close();
        free_address_info(local_addr_info);
        return HAKO_PDU_ERR_IO_ERROR;
    }
    free_address_info(local_addr_info);
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpCommMultiplexer::close() noexcept
{
    stop();
    SocketHandle current_listen_fd = listen_fd_.load();
    if (is_valid_socket(current_listen_fd)) {
        (void)close_socket(current_listen_fd);
        listen_fd_ = kInvalidSocket;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpCommMultiplexer::start() noexcept
{
    if (is_running_) {
        return HAKO_PDU_ERR_BUSY;
    }
    is_running_ = true;
    accept_thread_ = std::thread(&TcpCommMultiplexer::accept_loop_, this);
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType TcpCommMultiplexer::stop() noexcept
{
    if (!is_running_) {
        return HAKO_PDU_ERR_OK;
    }
    is_running_ = false;
    SocketHandle current_listen_fd = listen_fd_.load();
    if (is_valid_socket(current_listen_fd)) {
        shutdown_socket(current_listen_fd, SocketShutdownMode::Read);
        (void)close_socket(current_listen_fd);
        listen_fd_ = kInvalidSocket;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    return HAKO_PDU_ERR_OK;
}

std::vector<std::shared_ptr<PduComm>> TcpCommMultiplexer::take_sessions()
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::shared_ptr<PduComm>> out;
    out.swap(pending_sessions_);
    return out;
}

size_t TcpCommMultiplexer::connected_count() const noexcept
{
    return connected_clients_.load();
}

size_t TcpCommMultiplexer::expected_count() const noexcept
{
    return expected_clients_;
}

void TcpCommMultiplexer::accept_loop_()
{
    while (is_running_) {
        SocketAddressStorage client_addr{};
        SocketLength client_len = sizeof(client_addr);
        SocketHandle accepted_fd = accept_socket(listen_fd_.load(), reinterpret_cast<SocketAddress*>(&client_addr), &client_len);
        if (!is_valid_socket(accepted_fd)) {
            if (is_running_) {
                const int error_number = last_socket_error();
                log_tcp_socket_failure(
                    {"tcp_mux", comm_name_, "listener", 0, {}},
                    "accept",
                    error_number,
                    map_errno_to_error(error_number),
                    options_.read_timeout_ms,
                    !is_running_.load());
            }
            continue;
        }

        const auto connection_id = next_tcp_connection_id();
        const auto peer = format_socket_address(
            reinterpret_cast<SocketAddress*>(&client_addr), client_len);
        auto session = std::make_shared<TcpSessionComm>(accepted_fd, connection_id, peer);
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            pending_sessions_.push_back(std::move(session));
        }
        connected_clients_.fetch_add(1);
    }
}

HakoPduErrorType TcpCommMultiplexer::configure_socket_options_(SocketHandle fd, const Options& options) noexcept
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
    return HAKO_PDU_ERR_OK;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
