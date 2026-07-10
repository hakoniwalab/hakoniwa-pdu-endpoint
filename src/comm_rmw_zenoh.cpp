#include "hakoniwa/pdu/comm/comm_rmw_zenoh.hpp"

#include "hakoniwa/pdu/socket_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cctype>
#include <cstdlib>
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
constexpr const char* kRmwZenohDefaultQosKeyexpr = "::,:,:,:,,";

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

bool graph_debug_enabled()
{
    const char* value = std::getenv("HAKO_RMW_ZENOH_GRAPH_DEBUG");
    return value != nullptr && value[0] != '\0' && std::string(value) != "0";
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
    err = declare_node_liveliness_();
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to declare RMW Zenoh graph node liveliness: " << static_cast<int>(err) << std::endl;
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
    if (is_running_) {
        return HAKO_PDU_ERR_OK;
    }
    const auto graph_err = declare_topic_liveliness_();
    if (graph_err != HAKO_PDU_ERR_OK) {
        return graph_err;
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
    if (!mapping_can_publish_(mapping)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

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
    graph_config_ = GraphConfig{};
    graph_config_.node_name = sanitize_node_name_(config.value("name", std::string{"hakoniwa_pdu_endpoint"}));
    zenoh_config_path_ = rmw.at("config_path").get<std::string>();
    if (zenoh_config_path_.empty()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (rmw.contains("graph") && rmw.at("graph").is_object()) {
        const auto& graph = rmw.at("graph");
        graph_config_.enabled = graph.value("enabled", graph_config_.enabled);
        graph_config_.node_name = sanitize_node_name_(graph.value("node_name", graph_config_.node_name));
        graph_config_.node_namespace = graph.value("node_namespace", graph_config_.node_namespace);
        graph_config_.enclave = graph.value("enclave", graph_config_.enclave);
        graph_config_.session_id = graph.value("session_id", graph_config_.session_id);
        graph_config_.node_id = graph.value("node_id", graph_config_.node_id);
        graph_config_.qos = graph.value("qos", graph_config_.qos);
    }
    graph_config_.qos = normalize_graph_qos_(graph_config_.qos);
    if (graph_config_.node_name.empty()) {
        graph_config_.node_name = "hakoniwa_pdu_endpoint";
    }
    if (graph_config_.session_id.empty() || graph_config_.session_id == "auto") {
        graph_config_.session_id = make_graph_session_id_();
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
    graph_topic_tokens_.clear();

    std::uint32_t next_graph_entity_id = 1;
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
        mapping.direction = parse_direction(endpoint.value("direction", config.at("direction").get<std::string>()));
        mapping.pdu_name = endpoint.at("pdu").get<std::string>();
        mapping.notify_on_recv = endpoint.value("notify_on_recv", mapping_can_subscribe_(mapping));
        mapping.topic = ros2.at("topic").get<std::string>();
        mapping.type_hash = ros2.at("type_hash").get<std::string>();

        if (mapping.key.robot.empty() || mapping.pdu_name.empty() || mapping.topic.empty() || mapping.type_hash.empty()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (mapping_can_publish_(mapping) && mapping.type_hash == "*") {
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
        mapping.graph_enabled = graph_config_.enabled;
        if (mapping_can_publish_(mapping) && mapping_can_subscribe_(mapping)) {
            mapping.graph_pub_entity_id = next_graph_entity_id++;
            mapping.graph_sub_entity_id = next_graph_entity_id++;
        } else if (mapping_can_publish_(mapping)) {
            mapping.graph_pub_entity_id = next_graph_entity_id++;
        } else {
            mapping.graph_sub_entity_id = next_graph_entity_id++;
        }
        mapping.graph_qos = graph_config_.qos;
        if (ros2.contains("graph") && ros2.at("graph").is_object()) {
            const auto& graph = ros2.at("graph");
            mapping.graph_enabled = graph.value("enabled", mapping.graph_enabled);
            if (graph.contains("entity_id")) {
                const auto entity_id = graph.at("entity_id").get<std::uint32_t>();
                if (mapping_can_publish_(mapping)) {
                    mapping.graph_pub_entity_id = entity_id;
                }
                if (mapping_can_subscribe_(mapping)) {
                    mapping.graph_sub_entity_id =
                        (mapping_can_publish_(mapping) && mapping_can_subscribe_(mapping)) ? entity_id + 1 : entity_id;
                }
            }
            mapping.graph_qos = normalize_graph_qos_(graph.value("qos", mapping.graph_qos));
        }
        if (mapping.graph_qos.empty()) {
            mapping.graph_qos = kRmwZenohDefaultQosKeyexpr;
        }

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

    bool has_subscribe_mapping = false;
    for (const auto& mapping : mappings_) {
        if (mapping_can_subscribe_(mapping)) {
            has_subscribe_mapping = true;
            break;
        }
    }
    if (has_subscribe_mapping) {
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

HakoPduErrorType RmwZenohComm::declare_node_liveliness_() noexcept
{
    if (!graph_config_.enabled) {
        return HAKO_PDU_ERR_OK;
    }
    if (session_ == nullptr) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (graph_node_token_declared_) {
        return HAKO_PDU_ERR_OK;
    }
    const auto err = declare_liveliness_token_(make_graph_node_keyexpr_(), graph_node_token_);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }
    graph_node_token_declared_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::declare_topic_liveliness_() noexcept
{
    if (!graph_config_.enabled) {
        return HAKO_PDU_ERR_OK;
    }
    if (session_ == nullptr) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }

    if (graph_topic_tokens_.empty()) {
        graph_topic_tokens_.resize(mappings_.size() * 2);
    }
    std::size_t token_index = 0;
    for (const auto& mapping : mappings_) {
        if (!mapping.graph_enabled) {
            token_index += 2;
            continue;
        }
        if (mapping_can_publish_(mapping)) {
            if (token_index >= graph_topic_tokens_.size()) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            auto& token = graph_topic_tokens_.at(token_index++);
            if (!token.declared) {
                const auto err = declare_liveliness_token_(
                    make_graph_topic_keyexpr_(mapping, "MP", mapping.graph_pub_entity_id),
                    token.token);
                if (err != HAKO_PDU_ERR_OK) {
                    drop_graph_liveliness_();
                    return err;
                }
                token.declared = true;
            }
        } else {
            ++token_index;
        }
        if (mapping_can_subscribe_(mapping)) {
            if (token_index >= graph_topic_tokens_.size()) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            auto& token = graph_topic_tokens_.at(token_index++);
            if (!token.declared) {
                const auto err = declare_liveliness_token_(
                    make_graph_topic_keyexpr_(mapping, "MS", mapping.graph_sub_entity_id),
                    token.token);
                if (err != HAKO_PDU_ERR_OK) {
                    drop_graph_liveliness_();
                    return err;
                }
                token.declared = true;
            }
        } else {
            ++token_index;
        }
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType RmwZenohComm::declare_liveliness_token_(const std::string& keyexpr, z_owned_liveliness_token_t& token) noexcept
{
    if (graph_debug_enabled()) {
        std::cerr << "RMW Zenoh graph declare: " << keyexpr << std::endl;
    }
    z_owned_keyexpr_t ke;
    if (z_keyexpr_from_str(&ke, keyexpr.c_str()) < 0) {
        if (graph_debug_enabled()) {
            std::cerr << "RMW Zenoh graph declare failed: invalid keyexpr" << std::endl;
        }
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    const auto res = z_liveliness_declare_token(z_loan(*session_), &token, z_loan(ke), nullptr);
    z_drop(z_move(ke));
    if (graph_debug_enabled() && res != Z_OK) {
        std::cerr << "RMW Zenoh graph declare failed: zenoh result=" << res << std::endl;
    }
    return (res == Z_OK) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

void RmwZenohComm::drop_graph_liveliness_() noexcept
{
    for (auto& topic_token : graph_topic_tokens_) {
        if (topic_token.declared) {
            z_drop(z_move(topic_token.token));
            topic_token.declared = false;
        }
    }
    if (graph_node_token_declared_) {
        z_drop(z_move(graph_node_token_));
        graph_node_token_declared_ = false;
    }
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

std::string RmwZenohComm::make_graph_session_id_() const
{
    static std::atomic<std::uint64_t> counter{0};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const auto seq = counter.fetch_add(1, std::memory_order_relaxed);
    const auto hash = std::hash<std::string>{}(config_path_ + "#" + std::to_string(now) + "#" + std::to_string(seq));
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << static_cast<std::uint64_t>(hash);
    return oss.str();
}

std::string RmwZenohComm::make_graph_node_keyexpr_() const
{
    return "@ros2_lv/" + std::to_string(domain_id_)
        + "/" + graph_config_.session_id
        + "/" + std::to_string(graph_config_.node_id)
        + "/" + std::to_string(graph_config_.node_id)
        + "/NN/"
        + mangle_graph_name_(graph_config_.enclave)
        + "/" + mangle_graph_name_(graph_config_.node_namespace)
        + "/" + graph_config_.node_name;
}

std::string RmwZenohComm::make_graph_topic_keyexpr_(
    const Mapping& mapping,
    const std::string& entity_kind,
    std::uint32_t entity_id) const
{
    return "@ros2_lv/" + std::to_string(domain_id_)
        + "/" + graph_config_.session_id
        + "/" + std::to_string(graph_config_.node_id)
        + "/" + std::to_string(entity_id)
        + "/" + entity_kind
        + "/" + mangle_graph_name_(graph_config_.enclave)
        + "/" + mangle_graph_name_(graph_config_.node_namespace)
        + "/" + graph_config_.node_name
        + "/" + mangle_graph_topic_(mapping.topic)
        + "/" + mapping.type
        + "/" + mapping.type_hash
        + "/" + mapping.graph_qos;
}

std::string RmwZenohComm::mangle_graph_name_(const std::string& value) const
{
    if (value.empty() || value == "/") {
        return "%";
    }
    std::string out = value;
    for (char& ch : out) {
        if (ch == '/') {
            ch = '%';
        }
    }
    return out.empty() ? "%" : out;
}

std::string RmwZenohComm::mangle_graph_topic_(const std::string& topic) const
{
    std::string fq = topic.empty() || topic.front() == '/' ? topic : "/" + topic;
    for (char& ch : fq) {
        if (ch == '/') {
            ch = '%';
        }
    }
    return fq.empty() ? "%" : fq;
}

std::string RmwZenohComm::normalize_graph_qos_(const std::string& value) const
{
    if (value.empty() || value == "default") {
        return kRmwZenohDefaultQosKeyexpr;
    }
    return value;
}

std::string RmwZenohComm::sanitize_node_name_(const std::string& value) const
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    if (out.empty() || !std::isalpha(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), 'n');
    }
    return out;
}

void RmwZenohComm::cleanup_() noexcept
{
    is_running_ = false;
    is_open_ = false;
    drop_graph_liveliness_();
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
            if (mapping_can_subscribe_(candidate) && candidate.topic == topic && candidate.type == type
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
    if (!mapping_can_subscribe_(mapping)) {
        return;
    }
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

bool RmwZenohComm::mapping_can_publish_(const Mapping& mapping) const
{
    return mapping.direction == HAKO_PDU_ENDPOINT_DIRECTION_OUT
        || mapping.direction == HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
}

bool RmwZenohComm::mapping_can_subscribe_(const Mapping& mapping) const
{
    return mapping.direction == HAKO_PDU_ENDPOINT_DIRECTION_IN
        || mapping.direction == HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
