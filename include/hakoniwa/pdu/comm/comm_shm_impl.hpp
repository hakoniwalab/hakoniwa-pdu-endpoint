#pragma once
#include "hakoniwa/pdu/comm/comm.hpp"
#include "hakoniwa/pdu/endpoint_types.hpp"
#include <atomic>
#include <mutex>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

class PduCommShmImp {
public:
    virtual ~PduCommShmImp() = default;

    virtual HakoPduErrorType create_pdu_lchannel(const std::string& robot_name, HakoPduChannelIdType channel_id, size_t pdu_size) noexcept { return HAKO_PDU_ERR_UNSUPPORTED; };
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept = 0;
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept = 0;
    virtual HakoPduErrorType register_rcv_event(const PduResolvedKey& pdu_key, void (*on_recv)(int), int& out_event_id) noexcept = 0;
    virtual void process_recv_events() noexcept = 0;
};

class PduCommShmPollImpl : public PduCommShmImp {
public:
    PduCommShmPollImpl(std::shared_ptr<PduDefinition> pdu_def, const std::string& asset_name);
    virtual ~PduCommShmPollImpl();

    virtual HakoPduErrorType create_pdu_lchannel(const std::string& robot_name, HakoPduChannelIdType channel_id, size_t pdu_size) noexcept override;
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;
    virtual HakoPduErrorType register_rcv_event(const PduResolvedKey& pdu_key, void (*on_recv)(int), int& out_event_id) noexcept override;
    virtual void process_recv_events() noexcept override;
private:
    struct PollEntry {
        PduResolvedKey key{};
        int event_id{-1};
        void (*on_recv)(int){nullptr};
    };

    std::shared_ptr<PduDefinition> pdu_def_;
    std::string asset_name_;
    std::atomic<int> next_event_id_{0};
    std::mutex poll_mutex_;
    std::vector<PollEntry> poll_entries_;
};

class PduCommShmCallbackImpl : public PduCommShmImp {
public:
    PduCommShmCallbackImpl(std::shared_ptr<PduDefinition> pdu_def);
    virtual ~PduCommShmCallbackImpl();

    virtual HakoPduErrorType create_pdu_lchannel(const std::string& robot_name, HakoPduChannelIdType channel_id, size_t pdu_size) noexcept override;
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;
    virtual HakoPduErrorType register_rcv_event(const PduResolvedKey& pdu_key, void (*on_recv)(int), int& out_event_id) noexcept override;
    virtual void process_recv_events() noexcept override;
private:
    std::shared_ptr<PduDefinition> pdu_def_;
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
