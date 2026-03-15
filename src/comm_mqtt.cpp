#include "hakoniwa/pdu/comm/comm_mqtt.hpp"

#include "hakoniwa/pdu/socket_utils.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace hakoniwa {
namespace pdu {
namespace comm {

#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
class MqttComm::Callback final : public mqtt::callback
{
public:
    explicit Callback(MqttComm* owner)
    : owner_(owner)
    {
    }

    void connection_lost(const std::string& cause) override
    {
        if (owner_ == nullptr) {
            return;
        }
        owner_->notify_disconnected_(-1, cause.empty() ? "mqtt connection lost" : cause);
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        if (owner_ == nullptr) {
            return;
        }
        owner_->on_message_(std::move(msg));
    }

private:
    MqttComm* owner_{nullptr};
};
#endif

MqttComm::MqttComm() = default;

MqttComm::~MqttComm()
{
    (void)close();
}

HakoPduErrorType MqttComm::open(const std::string& config_path)
{
    if (is_open_) {
        return HAKO_PDU_ERR_BUSY;
    }
    const auto err = parse_config_(config_path);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }

#ifndef HAKO_PDU_ENDPOINT_HAS_MQTT
    return HAKO_PDU_ERR_UNSUPPORTED;
#else
    try {
        callback_ = std::make_shared<Callback>(this);
        client_ = std::make_unique<mqtt::async_client>(broker_, client_id_);
        client_->set_callback(*callback_);
    } catch (const mqtt::exception&) {
        callback_.reset();
        client_.reset();
        return HAKO_PDU_ERR_IO_ERROR;
    }

    is_open_ = true;
    return HAKO_PDU_ERR_OK;
#endif
}

HakoPduErrorType MqttComm::close() noexcept
{
    (void)stop();
#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
    callback_.reset();
    client_.reset();
#endif
    is_open_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType MqttComm::start() noexcept
{
    if (!is_open_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

#ifndef HAKO_PDU_ENDPOINT_HAS_MQTT
    return HAKO_PDU_ERR_UNSUPPORTED;
#else
    if (is_running_) {
        return HAKO_PDU_ERR_OK;
    }

    try {
        mqtt::connect_options options;
        client_->connect(options)->wait();
        if (direction_ != HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
            client_->subscribe(subscribe_topic_, qos_)->wait();
        }
    } catch (const mqtt::exception&) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    is_running_ = true;
    return HAKO_PDU_ERR_OK;
#endif
}

HakoPduErrorType MqttComm::stop() noexcept
{
#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
    if (client_ != nullptr) {
        try {
            if (client_->is_connected()) {
                if (direction_ != HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
                    client_->unsubscribe(subscribe_topic_)->wait();
                }
                client_->disconnect()->wait();
            }
        } catch (const mqtt::exception&) {
        }
    }
#endif
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType MqttComm::is_running(bool& running) noexcept
{
    running = is_running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType MqttComm::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
{
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

#ifndef HAKO_PDU_ENDPOINT_HAS_MQTT
    (void)pdu_key;
    (void)data;
    return HAKO_PDU_ERR_UNSUPPORTED;
#else
    try {
        const auto topic = make_topic_(pdu_key);
        auto message = mqtt::make_message(
            topic,
            reinterpret_cast<const void*>(data.data()),
            data.size());
        message->set_qos(qos_);
        message->set_retained(retain_);
        client_->publish(message)->wait();
    } catch (const mqtt::exception&) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
#endif
}

HakoPduErrorType MqttComm::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
{
    (void)pdu_key;
    (void)data;
    received_size = 0;
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType MqttComm::parse_config_(const std::string& config_path)
{
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    nlohmann::json config;
    try {
        ifs >> config;
    } catch (const nlohmann::json::exception&) {
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    if (!config.contains("protocol") || config.at("protocol").get<std::string>() != "mqtt") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!config.contains("direction") || !config.contains("mqtt") || !config.at("mqtt").is_object()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    const auto& mqtt = config.at("mqtt");
    if (!mqtt.contains("broker") || !mqtt.contains("topic_prefix")) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    direction_ = parse_direction(config.at("direction").get<std::string>());
    broker_ = mqtt.at("broker").get<std::string>();
    topic_prefix_ = normalize_prefix_(mqtt.at("topic_prefix").get<std::string>());
    qos_ = mqtt.value("qos", 0);
    retain_ = mqtt.value("retain", false);
    if (qos_ < 0 || qos_ > 2 || broker_.empty()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    client_id_ = mqtt.value("client_id", std::string{});
    if (client_id_.empty()) {
        const auto stem = std::filesystem::path(config_path).stem().string();
        client_id_ = stem.empty() ? "hakoniwa_pdu_endpoint_mqtt" : stem;
    }

    subscribe_topic_ = topic_prefix_ + "/#";
    return HAKO_PDU_ERR_OK;
}

std::string MqttComm::normalize_prefix_(std::string prefix) const
{
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        return "hakoniwa";
    }
    return prefix;
}

std::string MqttComm::make_topic_(const PduResolvedKey& key) const
{
    return topic_prefix_ + "/" + key.robot + "/" + std::to_string(key.channel_id);
}

bool MqttComm::parse_topic_(const std::string& topic, PduResolvedKey& out) const
{
    const std::string prefix = topic_prefix_ + "/";
    if (topic.rfind(prefix, 0) != 0) {
        return false;
    }

    const std::string suffix = topic.substr(prefix.size());
    const auto delim = suffix.rfind('/');
    if (delim == std::string::npos || delim == 0 || delim == suffix.size() - 1) {
        return false;
    }

    out.robot = suffix.substr(0, delim);
    try {
        out.channel_id = static_cast<HakoPduChannelIdType>(std::stoul(suffix.substr(delim + 1)));
    } catch (...) {
        return false;
    }
    return true;
}

#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
void MqttComm::on_message_(mqtt::const_message_ptr msg)
{
    if (!is_running_ || !on_recv_callback_ || msg == nullptr) {
        return;
    }

    PduResolvedKey key{};
    if (!parse_topic_(msg->get_topic(), key)) {
        return;
    }

    const auto payload = msg->get_payload_ref();
    const auto* payload_data = reinterpret_cast<const std::byte*>(payload.data());
    const auto payload_size = payload.size();
    on_recv_callback_(key, std::span<const std::byte>(payload_data, payload_size));
}
#endif

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
