#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"

#include <zenoh.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

class RmwZenohComm final : public PduComm
{
public:
    RmwZenohComm() = default;
    ~RmwZenohComm() override;

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;
    HakoPduErrorType is_running(bool& running) noexcept override;
    HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;
    HakoPduErrorType set_recv_event(const PduResolvedKey& pdu_key) noexcept override;

private:
    struct Mapping {
        PduResolvedKey key;
        std::string pdu_name;
        std::string topic;
        std::string type;
        std::string type_hash;
        std::string keyexpr;
        std::array<std::uint8_t, 16> gid{};
        std::int64_t sequence_number{0};
        bool notify_on_recv{true};
    };

    HakoPduErrorType parse_config_(const std::string& config_path);
    HakoPduErrorType open_session_();
    HakoPduErrorType make_attachment_(Mapping& mapping, z_owned_bytes_t& attachment) noexcept;
    std::string make_keyexpr_(const std::string& topic, const std::string& type, const std::string& type_hash) const;
    bool parse_keyexpr_(const std::string& keyexpr, std::string& out_topic, std::string& out_type, std::string& out_type_hash) const;
    std::string derive_rmw_type_(const std::string& pdu_type) const;
    std::array<std::uint8_t, 16> parse_or_make_gid_(const std::string& gid, const std::string& seed) const;
    void cleanup_() noexcept;
    static void on_sample_thunk_(z_loaned_sample_t* sample, void* context);
    void on_sample_(z_loaned_sample_t* sample);
    bool should_notify_on_recv_(const PduResolvedKey& key) const;

    HakoPduEndpointDirectionType direction_{HAKO_PDU_ENDPOINT_DIRECTION_OUT};
    std::string config_path_;
    std::string zenoh_config_path_;
    std::uint32_t domain_id_{0};
    std::string subscriber_keyexpr_;
    z_view_keyexpr_t subscriber_keyexpr_view_{};
    std::vector<Mapping> mappings_;
    std::map<std::pair<std::string, HakoPduChannelIdType>, std::size_t> key_to_mapping_;
    std::map<std::string, std::size_t> keyexpr_to_mapping_;
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
