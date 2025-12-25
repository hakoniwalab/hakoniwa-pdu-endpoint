#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/pdu_definition.hpp" 
#include <memory> 
#include <span>
#include <functional>

namespace hakoniwa {
namespace pdu {

// callbacks for communication


class PduComm
{
public:
    PduComm() = default;
    virtual ~PduComm() = default;
    // コピー・ムーブ禁止（ポリモーフィックな基底クラス）
    PduComm(const PduComm&) = delete;
    PduComm(PduComm&&) = delete;
    PduComm& operator=(const PduComm&) = delete;
    PduComm& operator=(PduComm&&) = delete;

    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    virtual HakoPduErrorType close() noexcept = 0;
    virtual HakoPduErrorType start() noexcept = 0;
    virtual HakoPduErrorType stop() noexcept = 0;
    virtual HakoPduErrorType is_running(bool& running) noexcept = 0;


    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept = 0;
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept = 0;

    virtual HakoPduErrorType set_on_recv_callback(
        std::function<void(const PduResolvedKey&, std::span<const std::byte>)> callback) noexcept
    {
        on_recv_callback_ = callback;
        return HAKO_PDU_ERR_OK;
    }
    
    // Set PDU definition and store it in the protected member
    virtual void set_pdu_definition(std::shared_ptr<PduDefinition> pdu_def) { pdu_def_ = pdu_def; }

protected:
    std::shared_ptr<PduDefinition>  pdu_def_; // Moved to base class
    //callbacks can be added here
    std::function<void(const PduResolvedKey&, std::span<const std::byte>)> on_recv_callback_;
};
} // namespace pdu
} // namespace hakoniwa