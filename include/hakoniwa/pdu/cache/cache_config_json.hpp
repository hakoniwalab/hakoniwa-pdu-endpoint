#pragma once

#include "hakoniwa/pdu/cache/cache_config.hpp"

#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

namespace hakoniwa {
namespace pdu {

inline const char* cache_mode_to_string(CacheMode mode) noexcept
{
    switch (mode) {
    case CacheMode::Latest:
        return "latest";
    case CacheMode::Queue:
        return "queue";
    }
    return "";
}

inline HakoPduErrorType cache_config_to_json(
    const CacheConfig& config,
    nlohmann::json& out) noexcept
{
    if (validate_cache_config(config) != HAKO_PDU_ERR_OK) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    out = {
        {"type", "buffer"},
        {"name", config.name},
        {"store", {{"mode", cache_mode_to_string(config.mode)}}},
    };
    if (config.mode == CacheMode::Queue) {
        out["store"]["depth"] = config.depth;
    }
    return HAKO_PDU_ERR_OK;
}

inline HakoPduErrorType cache_config_from_json(
    const nlohmann::json& input,
    CacheConfig& out) noexcept
{
    try {
        if (!input.is_object() ||
            !input.contains("name") || !input["name"].is_string() ||
            !input.contains("type") || !input["type"].is_string() ||
            input["type"].get<std::string>() != "buffer" ||
            !input.contains("store") || !input["store"].is_object() ||
            !input["store"].contains("mode") || !input["store"]["mode"].is_string()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        const auto name = input["name"].get<std::string>();
        const auto mode = input["store"]["mode"].get<std::string>();
        CacheConfig config;
        if (mode == "latest") {
            config = make_latest_cache(name);
        }
        else if (mode == "queue") {
            if (!input["store"].contains("depth") || !input["store"]["depth"].is_number_unsigned()) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            config = make_queue_cache(name, input["store"]["depth"].get<std::size_t>());
        }
        else {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        if (validate_cache_config(config) != HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        out = std::move(config);
        return HAKO_PDU_ERR_OK;
    }
    catch (...) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
}

inline HakoPduErrorType save_cache_config(
    const CacheConfig& config,
    const std::string& path) noexcept
{
    nlohmann::json json;
    const auto result = cache_config_to_json(config, json);
    if (result != HAKO_PDU_ERR_OK) {
        return result;
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    ofs << json.dump(2) << '\n';
    return ofs.good() ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_INVALID_CONFIG;
}

inline HakoPduErrorType load_cache_config(
    const std::string& path,
    CacheConfig& out) noexcept
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    try {
        nlohmann::json json;
        ifs >> json;
        return cache_config_from_json(json, out);
    }
    catch (const nlohmann::json::parse_error&) {
        return HAKO_PDU_ERR_INVALID_JSON;
    }
    catch (...) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
}

} // namespace pdu
} // namespace hakoniwa
