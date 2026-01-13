#include "hakoniwa/pdu/comm/comm_shm.hpp"
#include "hakoniwa_asset_polling.h"
#include <mutex>
#include <iostream>

namespace hakoniwa {
namespace pdu {
namespace comm {
PduCommShmPollImpl::PduCommShmPollImpl(std::shared_ptr<PduDefinition> pdu_def, const std::string& asset_name)
    : pdu_def_(pdu_def), asset_name_(asset_name)
{
}
PduCommShmPollImpl::~PduCommShmPollImpl()
{
}
HakoPduErrorType PduCommShmPollImpl::create_pdu_lchannel(const std::string& robot_name, HakoPduChannelIdType channel_id, size_t pdu_size) noexcept
{
    if (hakoniwa_asset_create_pdu_lchannel(robot_name.c_str(), channel_id, pdu_size) != 0) {
        std::cerr << "PduCommShmPollImpl Error: Failed to create PDU channel. Robot: " << robot_name << " Channel ID: " << channel_id << std::endl;
        return HAKO_PDU_ERR_IO_ERROR;
    }
    std::cout << "PduCommShmPollImpl: Created PDU channel. Robot: " << robot_name << " Channel ID: " << channel_id << " Size: " << pdu_size << std::endl;
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShmPollImpl::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
{
    if (hakoniwa_asset_write_pdu(asset_name_.c_str(), pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<const char*>(data.data()), data.size()) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShmPollImpl::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
{
    if (hakoniwa_asset_read_pdu(asset_name_.c_str(), pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<char*>(data.data()), data.size()) == 0) {
        received_size = data.size();
        return HAKO_PDU_ERR_OK;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}
HakoPduErrorType PduCommShmPollImpl::register_rcv_event(const PduResolvedKey& pdu_key, void (*on_recv)(int), int& out_event_id) noexcept
{
    if (hakoniwa_asset_register_data_recv_event(pdu_key.robot.c_str(), pdu_key.channel_id) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    PollEntry entry;
    entry.key = pdu_key;
    entry.event_id = ++next_event_id_;
    entry.on_recv = on_recv;

    {
        std::lock_guard<std::mutex> lock(poll_mutex_);
        poll_entries_.push_back(entry);
    }
    out_event_id = entry.event_id;
    return HAKO_PDU_ERR_OK;
}

void PduCommShmPollImpl::process_recv_events() noexcept
{
    std::vector<PollEntry> entries;
    {
        std::lock_guard<std::mutex> lock(poll_mutex_);
        entries = poll_entries_;
    }
    for (const auto& entry : entries) {
        if (hakoniwa_asset_check_data_recv_event(
                asset_name_.c_str(),
                entry.key.robot.c_str(),
                entry.key.channel_id) == 0) {
            if (entry.on_recv) {
                entry.on_recv(entry.event_id);
            }
        }
    }
}
} // namespace comm
} // namespace pdu
} // namespace hakoniwa
