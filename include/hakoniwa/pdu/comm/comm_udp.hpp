#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>

namespace hakoniwa {
namespace pdu {
namespace comm {

// UDP comm: connectionless transport with explicit direction and PDU key.
// Framing uses PduCommRaw (v1/v2) and a configured PDU key for routing.
class UdpComm final : public PduCommRaw
{
public:
    UdpComm();
    virtual ~UdpComm();

protected:
    HakoPduErrorType raw_open(const std::string& config_path) override;
    HakoPduErrorType raw_close() noexcept override;
    HakoPduErrorType raw_start() noexcept override;
    HakoPduErrorType raw_stop() noexcept override;
    HakoPduErrorType raw_is_running(bool& running) noexcept override;
    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override;

private:
    void recv_loop();

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

    std::atomic<SocketHandle> socket_fd_{kInvalidSocket};
    SocketAddressStorage dest_addr_{};
    SocketLength dest_addr_len_ = 0;
    bool has_fixed_remote_ = false;
    SocketAddressStorage last_client_addr_{};
    SocketLength last_client_addr_len_ = 0;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;

    std::thread recv_thread_;
    std::atomic<bool> is_running_flag_{false};
};

}  // namespace comm
}  // namespace pdu
}  // namespace hakoniwa
