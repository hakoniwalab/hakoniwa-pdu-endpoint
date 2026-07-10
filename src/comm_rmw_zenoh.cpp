#include "hakoniwa/pdu/comm/comm_rmw_zenoh.hpp"

#include "hakoniwa/pdu/socket_utils.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
using NotifyKey = std::pair<std::string, HakoPduChannelIdType>;
constexpr std::size_t kRmwZenohSequenceNumberSize = 8;
constexpr std::size_t kRmwZenohSourceTimestampSize = 8;
constexpr std::size_t kRmwZenohGidLengthSize = 1;
constexpr std::size_t kRmwZenohGidSize = 16;
constexpr std::size_t kRmwZenohAttachmentSize =
    kRmwZenohSequenceNumberSize + kRmwZenohSourceTimestampSize + kRmwZenohGidLengthSize + kRmwZenohGidSize;

std::string strip_slashes(std::string value)
{
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::uint8_t hex_byte(char high, char low)
{
    auto val = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<std::uint8_t>(10 + c - 'a');
        }
        return static_cast<std::uint8_t>(10 + c - 'A');
    };
    return static_cast<std::uint8_t>((val(high) << 4) | val(low));
}
} // namespace

RmwZenohComm::~RmwZenohComm()
{
    cleanup_();
}

HakoPduErrorType RmwZenohComm::open(const std::string& config_path)
{
    if (is_open_) {
        return HAKO_PDU_ERR_BUSY;
    }
    auto err = parse_config_(config_path);
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to parse RMW Zenoh comm config: " << static_cast<int>(err) << std::endl;
        return err;
    }
    err = open_session_();
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open RMW Zenoh session: " << static_cast<int>(err) << std::endl;
        cleanup_();
        return err;
    }
    is_open_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::close() noexcept
{
    cleanup_();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::start() noexcept
{
    if (!is_open_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    is_running_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::stop() noexcept
{
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::is_running(bool& running) noexcept
{
    running = is_running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
{
    if (!is_running_ || session_ == nullptr) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    auto it = key_to_mapping_.find(NotifyKey{pdu_key.robot, pdu_key.channel_id});
    if (it == key_to_mapping_.end()) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    auto& mapping = mappings_.at(it->second);

    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, mapping.keyexpr.c_str()) < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    z_owned_bytes_t payload;
    z_bytes_copy_from_buf(&payload, reinterpret_cast<const uint8_t*>(data.data()), data.size());

    z_owned_bytes_t attachment;
    auto attachment_err = make_attachment_(mapping, attachment);
    if (attachment_err != HAKO_PDU_ERR_OK) {
        z_drop(z_move(payload));
        return attachment_err;
    }

    z_put_options_t options;
    z_put_options_default(&options);
    options.attachment = z_move(attachment);

    const auto res = z_put(z_loan(*session_), z_loan(ke), z_move(payload), &options);
    return (res == Z_OK) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType RmwZenohComm::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
{
    (void)pdu_key;
    (void)data;
    received_size = 0;
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType RmwZenohComm::set_recv_event(const PduResolvedKey& pdu_key) noexcept
{
    explicit_recv_events_[NotifyKey{pdu_key.robot, pdu_key.channel_id}] = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::parse_config_(const std::string& config_path)
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

    if (!config.contains("protocol") || config.at("protocol").get<std::string>() != "rmw_zenoh") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!config.contains("direction") || !config.contains("rmw_zenoh") || !config.at("rmw_zenoh").is_object()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!pdu_def_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    const auto& rmw = config.at("rmw_zenoh");
    if (!rmw.contains("config_path") || !rmw.contains("mappings") || !rmw.at("mappings").is_array()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    direction_ = parse_direction(config.at("direction").get<std::string>());
    config_path_ = config_path;
    domain_id_ = rmw.value("domain_id", 0U);
    zenoh_config_path_ = rmw.at("config_path").get<std::string>();
    if (zenoh_config_path_.empty()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    {
        namespace fs = std::filesystem;
        fs::path config_dir = fs::path(config_path).parent_path();
        fs::path zenoh_cfg = fs::path(zenoh_config_path_);
        if (!zenoh_cfg.is_absolute()) {
            zenoh_cfg = (config_dir / zenoh_cfg).lexically_normal();
        }
        zenoh_config_path_ = zenoh_cfg.string();
    }

    mappings_.clear();
    key_to_mapping_.clear();
    keyexpr_to_mapping_.clear();

    for (const auto& item : rmw.at("mappings")) {
        if (!item.is_object() || !item.contains("endpoint") || !item.at("endpoint").is_object()
            || !item.contains("ros2") || !item.at("ros2").is_object()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        const auto& endpoint = item.at("endpoint");
        const auto& ros2 = item.at("ros2");
        if (!endpoint.contains("robot") || !endpoint.contains("pdu")
            || !ros2.contains("topic") || !ros2.contains("type_hash")) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        Mapping mapping;
        mapping.key.robot = endpoint.at("robot").get<std::string>();
        mapping.pdu_name = endpoint.at("pdu").get<std::string>();
        mapping.notify_on_recv = endpoint.value("notify_on_recv", true);
        mapping.topic = ros2.at("topic").get<std::string>();
        mapping.type_hash = ros2.at("type_hash").get<std::string>();

        if (mapping.key.robot.empty() || mapping.pdu_name.empty() || mapping.topic.empty() || mapping.type_hash.empty()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT && mapping.type_hash == "*") {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        PduDef def;
        if (!pdu_def_->resolve(mapping.key.robot, mapping.pdu_name, def)) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        mapping.key.channel_id = def.channel_id;

        const auto type = ros2.value("type", std::string{"auto"});
        mapping.type = (type == "auto") ? derive_rmw_type_(def.type) : type;
        if (mapping.type.empty() || mapping.type.find('/') != std::string::npos) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        mapping.keyexpr = make_keyexpr_(mapping.topic, mapping.type, mapping.type_hash);
        mapping.gid = parse_or_make_gid_(ros2.value("gid", std::string{"auto"}), mapping.keyexpr);

        const std::size_t index = mappings_.size();
        key_to_mapping_[NotifyKey{mapping.key.robot, mapping.key.channel_id}] = index;
        keyexpr_to_mapping_[mapping.keyexpr] = index;
        mappings_.push_back(std::move(mapping));
    }

    if (mappings_.empty()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::open_session_()
{
    auto* owned_session = new z_owned_session_t{};
    if (zc_config_from_file(&config_, zenoh_config_path_.c_str()) < 0) {
        delete owned_session;
        std::cerr << "Failed to load Zenoh config from file: " << zenoh_config_path_ << std::endl;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    if (z_open(owned_session, z_move(config_), nullptr) < 0) {
        delete owned_session;
        return HAKO_PDU_ERR_IO_ERROR;
    }

    if (direction_ != HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        subscriber_keyexpr_ = std::to_string(domain_id_) + "/**";
        if (z_view_keyexpr_from_str(&subscriber_keyexpr_view_, subscriber_keyexpr_.c_str()) < 0) {
            z_drop(z_move(*owned_session));
            delete owned_session;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        z_closure(&sample_callback_, &RmwZenohComm::on_sample_thunk_, nullptr, this);
        auto* owned_subscriber = new z_owned_subscriber_t{};
        if (z_declare_subscriber(z_loan(*owned_session), owned_subscriber, z_loan(subscriber_keyexpr_view_), z_move(sample_callback_), nullptr) < 0) {
            delete owned_subscriber;
            z_drop(z_move(*owned_session));
            delete owned_session;
            return HAKO_PDU_ERR_IO_ERROR;
        }
        subscriber_ = owned_subscriber;
    }

    session_ = owned_session;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::make_attachment_(Mapping& mapping, z_owned_bytes_t& attachment) noexcept
{
    // Compatibility boundary for the current rmw_zenoh data attachment format.
    // Keep this isolated because upstream rmw_zenoh may change the wire format.
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::array<std::uint8_t, kRmwZenohAttachmentSize> buffer{};
    auto put_le_i64 = [&buffer](std::size_t offset, std::int64_t value) {
        const auto v = static_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < 8; ++i) {
            buffer[offset + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xffU);
        }
    };

    put_le_i64(0, mapping.sequence_number++);
    put_le_i64(kRmwZenohSequenceNumberSize, static_cast<std::int64_t>(timestamp));
    buffer[kRmwZenohSequenceNumberSize + kRmwZenohSourceTimestampSize] = static_cast<std::uint8_t>(mapping.gid.size());
    std::memcpy(
        buffer.data() + kRmwZenohSequenceNumberSize + kRmwZenohSourceTimestampSize + kRmwZenohGidLengthSize,
        mapping.gid.data(),
        mapping.gid.size());

    z_bytes_copy_from_buf(&attachment, buffer.data(), buffer.size());
    return HAKO_PDU_ERR_OK;
}

std::string RmwZenohComm::make_keyexpr_(const std::string& topic, const std::string& type, const std::string& type_hash) const
{
    return std::to_string(domain_id_) + "/" + strip_slashes(topic) + "/" + type + "/" + type_hash;
}

bool RmwZenohComm::parse_keyexpr_(const std::string& keyexpr, std::string& out_topic, std::string& out_type, std::string& out_type_hash) const
{
    const std::string prefix = std::to_string(domain_id_) + "/";
    if (keyexpr.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string suffix = keyexpr.substr(prefix.size());
    const auto hash_delim = suffix.rfind('/');
    if (hash_delim == std::string::npos || hash_delim == 0 || hash_delim == suffix.size() - 1) {
        return false;
    }
    const auto type_delim = suffix.rfind('/', hash_delim - 1);
    if (type_delim == std::string::npos || type_delim == 0 || type_delim == hash_delim - 1) {
        return false;
    }

    out_topic = "/" + suffix.substr(0, type_delim);
    out_type = suffix.substr(type_delim + 1, hash_delim - type_delim - 1);
    out_type_hash = suffix.substr(hash_delim + 1);
    return true;
}

std::string RmwZenohComm::derive_rmw_type_(const std::string& pdu_type) const
{
    const auto delim = pdu_type.find('/');
    if (delim == std::string::npos || delim == 0 || delim == pdu_type.size() - 1) {
        return {};
    }
    const auto package = pdu_type.substr(0, delim);
    const auto message = pdu_type.substr(delim + 1);
    if (message.find('/') != std::string::npos) {
        return {};
    }
    return package + "::msg::dds_::" + message + "_";
}

std::array<std::uint8_t, 16> RmwZenohComm::parse_or_make_gid_(const std::string& gid, const std::string& seed) const
{
    std::array<std::uint8_t, 16> out{};
    if (gid.size() == out.size() * 2) {
        bool valid = true;
        for (char c : gid) {
            if (!is_hex(c)) {
                valid = false;
                break;
            }
        }
        if (valid) {
            for (std::size_t i = 0; i < out.size(); ++i) {
                out[i] = hex_byte(gid[i * 2], gid[i * 2 + 1]);
            }
            return out;
        }
    }

    std::hash<std::string> hasher;
    const auto h0 = hasher(seed);
    const auto h1 = hasher(seed + "#rmw_zenoh");
    const std::size_t hashes[2] = {h0, h1};
    for (std::size_t h = 0; h < 2; ++h) {
        for (std::size_t i = 0; i < 8; ++i) {
            out[(h * 8) + i] = static_cast<std::uint8_t>((hashes[h] >> (i * 8)) & 0xffU);
        }
    }
    return out;
}

void RmwZenohComm::cleanup_() noexcept
{
    is_running_ = false;
    is_open_ = false;
    if (subscriber_ != nullptr) {
        z_drop(z_move(*subscriber_));
        delete subscriber_;
        subscriber_ = nullptr;
    }
    if (session_ != nullptr) {
        z_drop(z_move(*session_));
        delete session_;
        session_ = nullptr;
    }
}

void RmwZenohComm::on_sample_thunk_(z_loaned_sample_t* sample, void* context)
{
    if (context == nullptr) {
        return;
    }
    static_cast<RmwZenohComm*>(context)->on_sample_(sample);
}

void RmwZenohComm::on_sample_(z_loaned_sample_t* sample)
{
    if (!on_recv_callback_) {
        return;
    }

    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);
    const std::string keyexpr(z_string_data(z_loan(key_string)), z_string_len(z_loan(key_string)));

    auto it = keyexpr_to_mapping_.find(keyexpr);
    if (it == keyexpr_to_mapping_.end()) {
        std::string topic;
        std::string type;
        std::string type_hash;
        if (!parse_keyexpr_(keyexpr, topic, type, type_hash)) {
            return;
        }
        for (std::size_t i = 0; i < mappings_.size(); ++i) {
            const auto& candidate = mappings_.at(i);
            if (candidate.topic == topic && candidate.type == type
                && (candidate.type_hash == type_hash || candidate.type_hash == "*")) {
                it = keyexpr_to_mapping_.emplace(keyexpr, i).first;
                break;
            }
        }
        if (it == keyexpr_to_mapping_.end()) {
            return;
        }
    }

    const auto& mapping = mappings_.at(it->second);
    if (!should_notify_on_recv_(mapping.key)) {
        return;
    }

    z_owned_slice_t slice;
    z_bytes_to_slice(z_sample_payload(sample), &slice);
    const auto* payload_data = reinterpret_cast<const std::byte*>(z_slice_data(z_loan(slice)));
    const auto payload_size = z_slice_len(z_loan(slice));
    on_recv_callback_(mapping.key, std::span<const std::byte>(payload_data, payload_size));
    z_drop(z_move(slice));
}

bool RmwZenohComm::should_notify_on_recv_(const PduResolvedKey& key) const
{
    auto explicit_it = explicit_recv_events_.find(NotifyKey{key.robot, key.channel_id});
    if (explicit_it != explicit_recv_events_.end()) {
        return explicit_it->second;
    }
    auto it = key_to_mapping_.find(NotifyKey{key.robot, key.channel_id});
    if (it == key_to_mapping_.end()) {
        return false;
    }
    return mappings_.at(it->second).notify_on_recv;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
