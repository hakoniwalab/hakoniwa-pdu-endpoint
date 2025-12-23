#pragma once

#include "hakoniwa/pdu/raw_endpoint.hpp"
#include <netinet/in.h>
#include <string>

namespace hakoniwa {
namespace pdu {

class UdpEndpoint final : public RawEndpoint
{
public:
    UdpEndpoint(const std::string& name, HakoPduEndpointDirectionType type);

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;
    HakoPduErrorType is_running(bool& running) noexcept override;
    HakoPduErrorType send(const void* data, size_t size) noexcept override;
    HakoPduErrorType recv(void* data, size_t buffer_size, size_t& received_size) noexcept override;

private:
    struct Options {
        int buffer_size = 8192;
        int timeout_ms = 1000;
        bool blocking = true;
        bool reuse_address = true;
        bool broadcast = false;
        bool multicast_enabled = false;
        std::string multicast_group;
        std::string multicast_interface = "0.0.0.0";
        int multicast_ttl = 1;
    };

    HakoPduErrorType configure_socket_options(const Options& options) noexcept;
    HakoPduErrorType configure_multicast(const Options& options) noexcept;

    int socket_fd_ = -1;
    bool running_ = false;
    sockaddr_storage dest_addr_{};
    socklen_t dest_addr_len_ = 0;
    bool has_fixed_remote_ = false;
    sockaddr_storage last_client_addr_{};
    socklen_t last_client_addr_len_ = 0;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
};

}  // namespace pdu
}  // namespace hakoniwa
