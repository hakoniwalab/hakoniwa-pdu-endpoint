#include "hakoniwa/pdu/comm/comm_shm_impl.hpp"

namespace hakoniwa {
namespace pdu {
namespace comm {

PduCommShmCallbackImpl::PduCommShmCallbackImpl(std::shared_ptr<PduDefinition> pdu_def,
    std::optional<std::string> io_asset_name,
    std::optional<std::string> pdu_config_path)
    : pdu_def_(std::move(pdu_def)),
      io_asset_name_(std::move(io_asset_name)),
      pdu_config_path_(std::move(pdu_config_path))
{
}

PduCommShmCallbackImpl::~PduCommShmCallbackImpl() = default;

HakoPduErrorType PduCommShmCallbackImpl::ensure_attached() noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmCallbackImpl::create_pdu_lchannel(const std::string&, HakoPduChannelIdType, size_t) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmCallbackImpl::send(const PduResolvedKey&, std::span<const std::byte>) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmCallbackImpl::recv(const PduResolvedKey&, std::span<std::byte>, size_t&) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmCallbackImpl::register_rcv_event(const PduResolvedKey&, void (*)(int), int&) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmCallbackImpl::attach_asset_context(
    const std::optional<std::string>&,
    const std::optional<std::string>&) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

void PduCommShmCallbackImpl::process_recv_events() noexcept
{
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
