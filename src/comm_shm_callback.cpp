#include "hakoniwa/pdu/comm/comm_shm.hpp"
#include "hako_asset.h"
#include <iostream>
#include <utility>

namespace hakoniwa {
namespace pdu {
namespace comm {
PduCommShmCallbackImpl::PduCommShmCallbackImpl(std::shared_ptr<PduDefinition> pdu_def,
    std::optional<std::string> io_asset_name,
    std::optional<std::string> pdu_config_path)
    : pdu_def_(pdu_def), io_asset_name_(std::move(io_asset_name)), pdu_config_path_(std::move(pdu_config_path))
{
}
PduCommShmCallbackImpl::~PduCommShmCallbackImpl()
{
}
HakoPduErrorType PduCommShmCallbackImpl::ensure_attached() noexcept
{
    if (attached_) {
        return HAKO_PDU_ERR_OK;
    }

    int rc = 0;
    if (io_asset_name_) {
        if (!pdu_config_path_) {
            std::cerr << "PduCommShmCallbackImpl Error: pdu_config_path is required for asset-context attach." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        rc = hako_asset_attach_core_with_name(io_asset_name_->c_str(), pdu_config_path_->c_str());
    } else {
        rc = hako_asset_attach_core();
    }
    if (rc != 0) {
        std::cerr << "PduCommShmCallbackImpl Error: Failed to attach Hakoniwa core. rc=" << rc << std::endl;
        return HAKO_PDU_ERR_IO_ERROR;
    }
    attached_ = true;
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShmCallbackImpl::create_pdu_lchannel(const std::string& robot_name, HakoPduChannelIdType channel_id, size_t pdu_size) noexcept
{
    const auto attach_err = ensure_attached();
    if (attach_err != HAKO_PDU_ERR_OK) {
        return attach_err;
    }
    if (hako_asset_pdu_create(robot_name.c_str(), channel_id, pdu_size) != 0) {
        std::cerr << "PduCommShmCallbackImpl Error: Failed to create PDU channel. Robot: " << robot_name << " Channel ID: " << channel_id << std::endl;
        return HAKO_PDU_ERR_IO_ERROR;
    }
    std::cout << "PduCommShmCallbackImpl: Created PDU channel. Robot: " << robot_name << " Channel ID: " << channel_id << " Size: " << pdu_size << std::endl;
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShmCallbackImpl::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
{
    const auto attach_err = ensure_attached();
    if (attach_err != HAKO_PDU_ERR_OK) {
        return attach_err;
    }
    if (hako_asset_pdu_write(pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<const char*>(data.data()), data.size()) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShmCallbackImpl::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
{
    const auto attach_err = ensure_attached();
    if (attach_err != HAKO_PDU_ERR_OK) {
        return attach_err;
    }
    if (hako_asset_pdu_read(pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<char*>(data.data()), data.size()) == 0) {
        received_size = data.size();
        return HAKO_PDU_ERR_OK;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}
HakoPduErrorType PduCommShmCallbackImpl::register_rcv_event(const PduResolvedKey& pdu_key, void (*on_recv)(int), int& out_event_id) noexcept
{
    const auto attach_err = ensure_attached();
    if (attach_err != HAKO_PDU_ERR_OK) {
        return attach_err;
    }
    if (hako_asset_register_data_recv_event(pdu_key.robot.c_str(), pdu_key.channel_id, on_recv, &out_event_id) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShmCallbackImpl::attach_asset_context(
    const std::optional<std::string>& io_asset_name,
    const std::optional<std::string>& pdu_config_path) noexcept
{
    if (attached_) {
        return HAKO_PDU_ERR_OK;
    }
    io_asset_name_ = io_asset_name;
    pdu_config_path_ = pdu_config_path;
    return HAKO_PDU_ERR_OK;
}

void PduCommShmCallbackImpl::process_recv_events() noexcept
{
}
} // namespace comm
} // namespace pdu
} // namespace hakoniwa
