#include "hakoniwa/pdu/comm/comm_zenoh.hpp"

#include "hakoniwa/pdu/socket_utils.hpp"

#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
using NotifyKey = std::pair<std::string, HakoPduChannelIdType>;
} // namespace

ZenohComm::~ZenohComm()
{
    cleanup_();
}

HakoPduErrorType ZenohComm::open(const std::string& config_path)
{
    if (is_open_) {
        return HAKO_PDU_ERR_BUSY;
    }
    auto err = parse_config_(config_path);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }
    err = open_session_();
    if (err != HAKO_PDU_ERR_OK) {
        cleanup_();
        return err;
    }
    is_open_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::close() noexcept
{
    cleanup_();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::start() noexcept
{
    if (!is_open_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    is_running_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::stop() noexcept
{
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::is_running(bool& running) noexcept
{
    running = is_running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
{
    if (!is_running_ || session_ == nullptr) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    const auto keyexpr = make_keyexpr_(pdu_key);
    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, keyexpr.c_str()) < 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    z_owned_bytes_t payload;
    z_bytes_copy_from_buf(&payload,
                          reinterpret_cast<const uint8_t*>(data.data()),
                          data.size());
    const auto res = z_put(z_loan(*session_), z_loan(ke), z_move(payload), nullptr);
    return (res == Z_OK) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType ZenohComm::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
{
    (void)pdu_key;
    (void)data;
    received_size = 0;
    return HAKO_PDU_ERR_UNSUPPORTED;
}

HakoPduErrorType ZenohComm::parse_config_(const std::string& config_path)
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

    if (!config.contains("protocol") || config.at("protocol").get<std::string>() != "zenoh") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!config.contains("direction") || !config.contains("zenoh") || !config.at("zenoh").is_object()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    const auto& zenoh = config.at("zenoh");
    direction_ = parse_direction(config.at("direction").get<std::string>());
    config_path_ = config_path;
    key_prefix_ = normalize_prefix_(zenoh.value("key_prefix", "hakoniwa"));
    zenoh_config_path_ = zenoh.value("config_path", std::string{});
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

    notify_on_recv_.clear();
    if (zenoh.contains("io")) {
        if (!pdu_def_) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        const auto& io = zenoh.at("io");
        if (!io.is_object() || !io.contains("robots") || !io.at("robots").is_array()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        for (const auto& robot : io.at("robots")) {
            if (!robot.is_object() || !robot.contains("name") || !robot.contains("pdu")) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            const auto robot_name = robot.at("name").get<std::string>();
            if (!robot.at("pdu").is_array()) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            for (const auto& pdu : robot.at("pdu")) {
                if (!pdu.is_object() || !pdu.contains("name") || !pdu.contains("notify_on_recv")) {
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                PduDef def;
                if (!pdu_def_->resolve(robot_name, pdu.at("name").get<std::string>(), def)) {
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                notify_on_recv_[NotifyKey{robot_name, def.channel_id}] = pdu.at("notify_on_recv").get<bool>();
            }
        }
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType ZenohComm::open_session_()
{
    auto* owned_session = new z_owned_session_t{};
    if (zc_config_from_file(&config_, zenoh_config_path_.c_str()) < 0) {
        delete owned_session;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    if (z_open(owned_session, z_move(config_), nullptr) < 0) {
        delete owned_session;
        return HAKO_PDU_ERR_IO_ERROR;
    }

    if (direction_ != HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        subscriber_keyexpr_ = key_prefix_ + "/**";
        if (z_view_keyexpr_from_str(&subscriber_keyexpr_view_, subscriber_keyexpr_.c_str()) < 0) {
            z_drop(z_move(*owned_session));
            delete owned_session;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        z_closure(&sample_callback_, &ZenohComm::on_sample_thunk_, nullptr, this);
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

std::string ZenohComm::make_keyexpr_(const PduResolvedKey& key) const
{
    return key_prefix_ + "/" + key.robot + "/" + std::to_string(key.channel_id);
}

bool ZenohComm::parse_keyexpr_(const std::string& keyexpr, PduResolvedKey& out) const
{
    const std::string prefix = key_prefix_ + "/";
    if (keyexpr.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string suffix = keyexpr.substr(prefix.size());
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

std::string ZenohComm::normalize_prefix_(std::string prefix) const
{
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        return "hakoniwa";
    }
    return prefix;
}

void ZenohComm::cleanup_() noexcept
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

void ZenohComm::on_sample_thunk_(z_loaned_sample_t* sample, void* context)
{
    if (context == nullptr) {
        return;
    }
    static_cast<ZenohComm*>(context)->on_sample_(sample);
}

void ZenohComm::on_sample_(z_loaned_sample_t* sample)
{
    if (!on_recv_callback_) {
        return;
    }

    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);
    const std::string keyexpr(z_string_data(z_loan(key_string)), z_string_len(z_loan(key_string)));

    PduResolvedKey key{};
    if (!parse_keyexpr_(keyexpr, key)) {
        return;
    }
    if (!should_notify_on_recv_(key)) {
        return;
    }

    z_owned_slice_t slice;
    z_bytes_to_slice(z_sample_payload(sample), &slice);
    const auto* payload_data = reinterpret_cast<const std::byte*>(z_slice_data(z_loan(slice)));
    const auto payload_size = z_slice_len(z_loan(slice));
    on_recv_callback_(key, std::span<const std::byte>(payload_data, payload_size));
    z_drop(z_move(slice));
}

bool ZenohComm::should_notify_on_recv_(const PduResolvedKey& key) const
{
    if (notify_on_recv_.empty()) {
        return true;
    }
    auto it = notify_on_recv_.find(NotifyKey{key.robot, key.channel_id});
    if (it == notify_on_recv_.end()) {
        return false;
    }
    return it->second;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
