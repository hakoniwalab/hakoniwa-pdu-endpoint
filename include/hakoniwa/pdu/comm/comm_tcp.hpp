#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include <netinet/in.h>
#include <netdb.h> // For addrinfo
#include <thread>
#include <atomic>
#include <vector>
#include <string>

namespace hakoniwa {
namespace pdu {
namespace comm {

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

    // Helper methods
    HakoPduErrorType read_data(int fd, std::byte* buffer, size_t size) noexcept;
    HakoPduErrorType write_data(int fd, const std::byte* buffer, size_t size) noexcept;

    enum class Role {
        Client,
        Server
    };

    struct Options {
        int backlog = 5;
        int connect_timeout_ms = 1000;
        int read_timeout_ms = 1000;
        int write_timeout_ms = 1000;
        bool blocking = true;
        bool reuse_address = true;
        bool keepalive = true;
        bool no_delay = true;
        int recv_buffer_size = 8192;
        int send_buffer_size = 8192;
        bool linger_enabled = false;
        int linger_timeout_sec = 0;
    };
    HakoPduErrorType configure_socket_options(int fd, const Options& options) noexcept;
    HakoPduErrorType configure_timeouts(int fd, const Options& options) noexcept;
    HakoPduErrorType connect_with_timeout(int fd, addrinfo* remote_addr, const Options& options) noexcept;

    // TCP specific state
    Role role_ = Role::Client;
    Options options_{};
    int listen_fd_ = -1;
    int client_fd_ = -1; // Represents the connected socket for both client and server
    
    // Threading
    std::thread comm_thread_;
    std::atomic<bool> is_running_flag_{false};

    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
    sockaddr_storage remote_addr_info_{};
    socklen_t remote_addr_len_ = 0;
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
