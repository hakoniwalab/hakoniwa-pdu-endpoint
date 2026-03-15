#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"

#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
#include <mqtt/async_client.h>
#endif

#include <memory>
#include <string>

namespace hakoniwa {
namespace pdu {
namespace comm {

class MqttComm final : public PduComm
{
public:
    MqttComm();
    ~MqttComm() override;

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;
    HakoPduErrorType is_running(bool& running) noexcept override;
    HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override;
    HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;

private:
    HakoPduErrorType parse_config_(const std::string& config_path);
    std::string normalize_prefix_(std::string prefix) const;
    std::string make_topic_(const PduResolvedKey& key) const;
    bool parse_topic_(const std::string& topic, PduResolvedKey& out) const;
#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
    class Callback;
    void on_message_(mqtt::const_message_ptr msg);
#endif

    HakoPduEndpointDirectionType direction_{HAKO_PDU_ENDPOINT_DIRECTION_OUT};
    std::string broker_;
    std::string client_id_;
    std::string topic_prefix_{"hakoniwa"};
    int qos_{0};
    bool retain_{false};
    std::string subscribe_topic_;

#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
    std::unique_ptr<mqtt::async_client> client_;
    std::shared_ptr<Callback> callback_;
#endif
    bool is_open_{false};
    bool is_running_{false};
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
