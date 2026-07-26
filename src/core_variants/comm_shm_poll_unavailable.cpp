#include "hakoniwa/pdu/comm/comm_shm_impl.hpp"

namespace hakoniwa {
namespace pdu {
namespace comm {

PduCommShmPollImpl::PduCommShmPollImpl(std::shared_ptr<PduDefinition> pdu_def, const std::string& asset_name)
    : pdu_def_(std::move(pdu_def)), asset_name_(asset_name)
{
}

PduCommShmPollImpl::~PduCommShmPollImpl() = default;

HakoPduErrorType PduCommShmPollImpl::create_pdu_lchannel(const std::string&, HakoPduChannelIdType, size_t) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmPollImpl::send(const PduResolvedKey&, std::span<const std::byte>) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmPollImpl::recv(const PduResolvedKey&, std::span<std::byte>, size_t&) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType PduCommShmPollImpl::register_rcv_event(const PduResolvedKey&, void (*)(int), int&) noexcept
{
    return HAKO_PDU_ERR_UNSUPPORTED;
}

void PduCommShmPollImpl::process_recv_events() noexcept
{
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
