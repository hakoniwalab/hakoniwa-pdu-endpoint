#pragma once

#include "hakoniwa/pdu/cache/cache.hpp"
#include "hakoniwa/pdu/comm/comm.hpp"
#include <memory>
#include <string>

namespace hakoniwa {
namespace pdu {

/**
 * @brief Creates a PDU cache instance based on the provided configuration file.
 * @param config_path Path to the cache configuration JSON file.
 * @return A unique_ptr to the created PduCache, or nullptr on failure.
 */
std::unique_ptr<PduCache> create_pdu_cache(const std::string& config_path);

/**
 * @brief Creates a PDU communication instance based on the provided configuration file.
 * @param config_path Path to the communication configuration JSON file.
 * @return A unique_ptr to the created PduComm, or nullptr on failure.
 */
std::unique_ptr<PduComm> create_pdu_comm(const std::string& config_path);

} // namespace pdu
} // namespace hakoniwa
