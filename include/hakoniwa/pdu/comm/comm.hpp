#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/pdu_definition.hpp" 
#include <memory> 
#include <span>
#include <functional>

namespace hakoniwa {
namespace pdu {

// callbacks for communication


// PduComm defines the transport contract used by Endpoint.
// Implementations must make delivery semantics explicit via config.
class PduComm : public std::enable_shared_from_this<PduComm>
{
public:
    PduComm() = default;
    virtual ~PduComm() = default;
    // コピー・ムーブ禁止（ポリモーフィックな基底クラス）
    PduComm(const PduComm&) = delete;
    PduComm(PduComm&&) = delete;
    PduComm& operator=(const PduComm&) = delete;
    PduComm& operator=(PduComm&&) = delete;

    // Optional pre-open hook for comms that must create PDU channels in advance.
    // Callers may skip this and just use open(); implementations should handle both.
    virtual HakoPduErrorType create_pdu_lchannels(const std::string& config_path) { return HAKO_PDU_ERR_OK; }
    // Load comm configuration. Must be callable once per instance.
    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    // Close and release resources. Should be idempotent.
    virtual HakoPduErrorType close() noexcept = 0;
    // Start background processing if needed.
    virtual HakoPduErrorType start() noexcept = 0;
    // Optional post-start hook for comms that need extra setup after start().
    virtual HakoPduErrorType post_start() noexcept { return HAKO_PDU_ERR_OK; }
    // Stop background processing if needed.
    virtual HakoPduErrorType stop() noexcept = 0;
    // Report running state.
    virtual HakoPduErrorType is_running(bool& running) noexcept = 0;


    // Send PDU data for a resolved key.
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept = 0;
    // Recv PDU data for a resolved key (optional; raw comms may return UNSUPPORTED).
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept = 0;

    virtual HakoPduErrorType set_on_recv_callback(
        std::function<void(const PduResolvedKey&, std::span<const std::byte>)> callback) noexcept
    {
        on_recv_callback_ = callback;
        return HAKO_PDU_ERR_OK;
    }

    // Only meaningful for SHM poll implementations. Other comm types are no-op.
    virtual void process_recv_events() noexcept {}
    
    // Set PDU definition and store it in the protected member
    virtual void set_pdu_definition(std::shared_ptr<PduDefinition> pdu_def) { pdu_def_ = pdu_def; }

protected:
    std::shared_ptr<PduDefinition>  pdu_def_; // Moved to base class
    //callbacks can be added here
    std::function<void(const PduResolvedKey&, std::span<const std::byte>)> on_recv_callback_;
};
} // namespace pdu
} // namespace hakoniwa
