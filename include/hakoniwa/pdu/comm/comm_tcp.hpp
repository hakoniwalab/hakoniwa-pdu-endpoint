#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

namespace hakoniwa {
namespace pdu {
namespace comm {

// TCP comm: stream-based transport with optional client/server role.
// Packet framing is handled by PduCommRaw (v1/v2).
class TcpComm final : public PduCommRaw
{
public:
    TcpComm();
    virtual ~TcpComm();

protected:
    // PduCommRaw's pure virtual methods implementation
    HakoPduErrorType raw_open(const std::string& config_path) override;
    HakoPduErrorType raw_close() noexcept override;
    HakoPduErrorType raw_start() noexcept override;
    HakoPduErrorType raw_stop() noexcept override;
    HakoPduErrorType raw_is_running(bool& running) noexcept override;
    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override;

private:
    // Main loop for client/server threads
    void server_loop();
    void client_loop();
    void notify_disconnect_if_needed_(HakoPduErrorType reason, const char* context) noexcept;

    // Helper methods
    HakoPduErrorType read_data(SocketHandle fd, std::byte* buffer, size_t size, const char* operation) noexcept;
    HakoPduErrorType write_data(SocketHandle fd, const std::byte* buffer, size_t size, const char* operation) noexcept;
    void close_failed_connection_(SocketHandle expected_fd) noexcept;

    enum class Role {
        Client,
        Server
    };

    struct Options {
        int backlog = 5;
        int connect_timeout_ms = 1000;
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
    HakoPduErrorType configure_socket_options(SocketHandle fd, const Options& options) noexcept;
    HakoPduErrorType configure_timeouts(SocketHandle fd, const Options& options) noexcept;
    HakoPduErrorType connect_with_timeout(SocketHandle fd, AddressInfo* remote_addr, const Options& options) noexcept;

    // TCP specific state
    Role role_ = Role::Client;
    Options options_{};
    std::atomic<SocketHandle> listen_fd_{kInvalidSocket};
    std::atomic<SocketHandle> client_fd_{kInvalidSocket}; // Represents the connected socket for both client and server
    
    // Threading
    std::thread comm_thread_;
    std::atomic<bool> is_running_flag_{false};

    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
    SocketAddressStorage remote_addr_info_{};
    SocketLength remote_addr_len_ = 0;

    std::atomic<bool> is_connected_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> disconnect_notified_{false};
    std::string comm_name_{"tcp"};
    std::string peer_endpoint_;
    std::atomic<std::uint64_t> connection_id_{0};
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
