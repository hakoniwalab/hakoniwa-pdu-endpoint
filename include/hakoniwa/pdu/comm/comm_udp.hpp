#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp" // Change base class
#include "hakoniwa/pdu/endpoint_types.hpp"
#include <netinet/in.h>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <memory> // std::unique_ptr for DataPacket in PduCommRaw

namespace hakoniwa {
namespace pdu {
namespace comm { // Add comm namespace

// UDP comm: connectionless transport with explicit direction and PDU key.
// Framing uses PduCommRaw (v1/v2) and a configured PDU key for routing.
class UdpComm final : public PduCommRaw // Change base class
{
public:
    UdpComm();
    virtual ~UdpComm();

protected: // Implement PduCommRaw's pure virtual raw_* methods
    HakoPduErrorType raw_open(const std::string& config_path) override;
    HakoPduErrorType raw_close() noexcept override;
    HakoPduErrorType raw_start() noexcept override;
    HakoPduErrorType raw_stop() noexcept override;
    HakoPduErrorType raw_is_running(bool& running) noexcept override;
    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override; // Added noexcept
    // recv is now handled by PduCommRaw

private:
    // 受信スレッドのメインループ
    void recv_loop();

    // 内部オプション構造体 (remains the same)
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

    // ソケットとアドレス関連 (remains the same)
    std::atomic<int> socket_fd_{-1};
    sockaddr_storage dest_addr_{};
    socklen_t dest_addr_len_ = 0;
    bool has_fixed_remote_ = false;
    sockaddr_storage last_client_addr_{};
    socklen_t last_client_addr_len_ = 0;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;

    // PDUキー for this endpoint
    hakoniwa::pdu::PduResolvedKey pdu_key_;

    // スレッド関連 (pdu_key_ is now in PduCommRaw)
    std::thread recv_thread_;
    std::atomic<bool> is_running_flag_{false}; // Renamed to avoid confusion with raw_is_running
    // queue_mtx_, queue_cv_, data_queue_ removed (now in PduCommRaw)
};

}  // namespace comm
}  // namespace pdu
}  // namespace hakoniwa
