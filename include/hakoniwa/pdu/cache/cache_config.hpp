#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"

#include <cstddef>
#include <string>

namespace hakoniwa {
namespace pdu {

enum class CacheMode {
    Latest,
    Queue,
};

struct CacheConfig {
    std::string name;
    CacheMode mode = CacheMode::Latest;
    std::size_t depth = 1;
};

inline constexpr std::size_t HAKO_PDU_CACHE_NAME_MAX_LENGTH = 256;
inline constexpr std::size_t HAKO_PDU_CACHE_QUEUE_DEPTH_MIN = 1;
inline constexpr std::size_t HAKO_PDU_CACHE_QUEUE_DEPTH_MAX = 1024;

inline HakoPduErrorType validate_cache_config(const CacheConfig& config) noexcept
{
    if (config.name.empty() || config.name.size() > HAKO_PDU_CACHE_NAME_MAX_LENGTH) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    switch (config.mode) {
    case CacheMode::Latest:
        return HAKO_PDU_ERR_OK;
    case CacheMode::Queue:
        if (config.depth < HAKO_PDU_CACHE_QUEUE_DEPTH_MIN ||
            config.depth > HAKO_PDU_CACHE_QUEUE_DEPTH_MAX) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        return HAKO_PDU_ERR_OK;
    }

    return HAKO_PDU_ERR_INVALID_CONFIG;
}

inline CacheConfig make_latest_cache(std::string name)
{
    return CacheConfig{
        .name = std::move(name),
        .mode = CacheMode::Latest,
        .depth = 1,
    };
}

inline CacheConfig make_queue_cache(std::string name, std::size_t depth)
{
    return CacheConfig{
        .name = std::move(name),
        .mode = CacheMode::Queue,
        .depth = depth,
    };
}

} // namespace pdu
} // namespace hakoniwa
