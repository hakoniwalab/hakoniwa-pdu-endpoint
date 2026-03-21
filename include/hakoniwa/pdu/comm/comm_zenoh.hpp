#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"
#include <zenoh.h>

#include <string>
#include <map>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

class ZenohComm final : public PduComm
{
public:
    ZenohComm() = default;
    ~ZenohComm() override;

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;
    HakoPduErrorType is_running(bool& running) noexcept override;
    HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;
    HakoPduErrorType set_recv_event(const PduResolvedKey& pdu_key) noexcept override;

private:
    HakoPduErrorType parse_config_(const std::string& config_path);
    HakoPduErrorType open_session_();
    std::string make_keyexpr_(const PduResolvedKey& key) const;
    bool parse_keyexpr_(const std::string& keyexpr, PduResolvedKey& out) const;
    std::string normalize_prefix_(std::string prefix) const;
    void cleanup_() noexcept;
    static void on_sample_thunk_(z_loaned_sample_t* sample, void* context);
    void on_sample_(z_loaned_sample_t* sample);
    bool should_notify_on_recv_(const PduResolvedKey& key) const;

    HakoPduEndpointDirectionType direction_{HAKO_PDU_ENDPOINT_DIRECTION_OUT};
	    std::string config_path_;
	    std::string key_prefix_{"hakoniwa"};
	    std::string zenoh_config_path_;
	    std::string subscriber_keyexpr_;
	    z_view_keyexpr_t subscriber_keyexpr_view_{};
	    std::map<std::pair<std::string, HakoPduChannelIdType>, bool> notify_on_recv_;
        std::map<std::pair<std::string, HakoPduChannelIdType>, bool> explicit_recv_events_;

	    z_owned_config_t config_{};
	    z_owned_closure_sample_t sample_callback_{};
	    z_owned_session_t* session_{nullptr};
	    z_owned_subscriber_t* subscriber_{nullptr};
	    bool is_open_{false};
	    bool is_running_{false};
	};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
