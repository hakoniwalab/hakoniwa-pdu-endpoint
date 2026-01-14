#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"
#include "hakoniwa/pdu/comm/comm_shm_impl.hpp"
#include "hakoniwa/pdu/endpoint_types.hpp"
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <map>

namespace hakoniwa {
namespace pdu {
namespace comm {

class PduCommShm : public PduComm {
public:
    PduCommShm();
    virtual ~PduCommShm();

    // PduComm virtual methods
    virtual HakoPduErrorType create_pdu_lchannels(const std::string& config_path) override;
    virtual HakoPduErrorType open(const std::string& config_path) override;
    virtual HakoPduErrorType close() noexcept override;
    virtual HakoPduErrorType start() noexcept override;
    virtual HakoPduErrorType post_start() noexcept override;
    virtual HakoPduErrorType stop() noexcept override;
    virtual HakoPduErrorType is_running(bool& running) noexcept override;
    // Only meaningful for SHM poll implementation; other SHM modes are no-op.
    virtual void process_recv_events() noexcept override;

    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;
    
private:
    // Callback from hako_asset C API
    static void shm_recv_callback(int recv_event_id);
    HakoPduErrorType native_send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept;
    HakoPduErrorType native_recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept;

    // Member function to handle the dispatched callback
    void handle_shm_recv(int recv_event_id);

    std::atomic<bool>                   running_;
    
    // Map from SHM event ID back to our internal key and the PduCommShm instance
    static std::map<int, PduCommShm*> event_id_to_instance_map_;
    static std::mutex event_map_mutex_;
    std::mutex io_mutex_;

    std::map<int, PduResolvedKey> event_id_to_key_map_;
    std::vector<int> registered_event_ids_;
    std::vector<PduResolvedKey> recv_notify_keys_;
    bool recv_events_registered_;
    std::unique_ptr<PduCommShmImp> impl_;
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
