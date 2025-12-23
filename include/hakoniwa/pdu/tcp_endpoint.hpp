#pragma once

#include "hakoniwa/pdu/endpoint.hpp"
#include <netinet/in.h>
#include <string>

namespace hakoniwa {
namespace pdu {

class TcpEndpoint final : public Endpoint
{
public:
    TcpEndpoint(const std::string& name, HakoPduEndpointDirectionType type);

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;
    HakoPduErrorType is_running(bool& running) noexcept override;
    HakoPduErrorType send(const void* data, size_t size) noexcept override;
    HakoPduErrorType recv(void* data, size_t buffer_size, size_t& received_size) noexcept override;

private:
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
    HakoPduErrorType accept_client() noexcept;
    HakoPduErrorType ensure_connected() noexcept;

    int listen_fd_ = -1;
    int socket_fd_ = -1;
    bool running_ = false;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
    Role role_ = Role::Client;
    Options options_{};
    sockaddr_storage local_addr_{};
    socklen_t local_addr_len_ = 0;
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_ = 0;
};

}  // namespace pdu
}  // namespace hakoniwa
