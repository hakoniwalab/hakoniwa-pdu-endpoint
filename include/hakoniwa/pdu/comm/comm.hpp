#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/pdu_definition.hpp" 
#include <memory> 
#include <span>
#include <functional>
#include <string>

namespace hakoniwa {
namespace pdu {

// callbacks for communication
struct CommDisconnectEvent {
    int reason_code;
    std::string reason_text;
};

using OnCommDisconnected = std::function<void(const CommDisconnectEvent&)>;

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
    virtual HakoPduErrorType create_pdu_lchannels(const std::string& config_path)
    {
        (void)config_path;
        return HAKO_PDU_ERR_OK;
    }
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
    // Queue-oriented receive API for time-ordered record consumption.
    virtual HakoPduErrorType recv_next(PduRecord& out) noexcept
    {
        (void)out;
        return HAKO_PDU_ERR_UNSUPPORTED;
    }
    virtual HakoPduErrorType set_recv_event(const PduResolvedKey& pdu_key) noexcept
    {
        (void)pdu_key;
        return HAKO_PDU_ERR_OK;
    }

    virtual HakoPduErrorType set_on_recv_callback(
        std::function<void(const PduResolvedKey&, std::span<const std::byte>)> callback) noexcept
    {
        on_recv_callback_ = callback;
        return HAKO_PDU_ERR_OK;
    }
    virtual HakoPduErrorType set_on_disconnected_callback(OnCommDisconnected callback) noexcept
    {
        on_disconnected_callback_ = std::move(callback);
        return HAKO_PDU_ERR_OK;
    }

    // Only meaningful for SHM poll implementations. Other comm types are no-op.
    virtual void process_recv_events() noexcept {}
    
    // Set PDU definition and store it in the protected member
    virtual void set_pdu_definition(std::shared_ptr<PduDefinition> pdu_def) { pdu_def_ = pdu_def; }
    // Optional asset-context binding for comms that need Hakoniwa asset-side PDU I/O.
    // asset_name == nullptr means external PDU access.
    virtual HakoPduErrorType attach_asset_context(const char* asset_name, const char* pdu_config_path) noexcept
    {
        (void)asset_name;
        (void)pdu_config_path;
        return HAKO_PDU_ERR_OK;
    }

protected:
    std::shared_ptr<PduDefinition>  pdu_def_; // Moved to base class
    //callbacks can be added here
    std::function<void(const PduResolvedKey&, std::span<const std::byte>)> on_recv_callback_;
    OnCommDisconnected on_disconnected_callback_;

    void notify_disconnected_(int reason_code, std::string reason_text) noexcept
    {
        if (!on_disconnected_callback_) {
            return;
        }
        on_disconnected_callback_({reason_code, std::move(reason_text)});
    }
};
} // namespace pdu
} // namespace hakoniwa
