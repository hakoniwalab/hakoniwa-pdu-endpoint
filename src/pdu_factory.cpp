#include "hakoniwa/pdu/pdu_factory.hpp"
#include "hakoniwa/pdu/cache/cache_buffer.hpp"
#include "hakoniwa/pdu/cache/cache_queue.hpp"
#include "hakoniwa/pdu/cache/cache_config_json.hpp"
#include "hakoniwa/pdu/comm/comm_tcp.hpp"
#include "hakoniwa/pdu/comm/comm_udp.hpp"
#ifdef HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE
#include "hakoniwa/pdu/comm/comm_shm.hpp"
#endif
#include "hakoniwa/pdu/comm/comm_websocket.hpp"
#include "hakoniwa/pdu/comm/comm_storage.hpp"
#ifdef HAKO_PDU_ENDPOINT_HAS_ZENOH
#include "hakoniwa/pdu/comm/comm_zenoh.hpp"
#include "hakoniwa/pdu/comm/comm_rmw_zenoh.hpp"
#endif
#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
#include "hakoniwa/pdu/comm/comm_mqtt.hpp"
#endif
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace hakoniwa {
namespace pdu {

std::unique_ptr<PduCache> create_pdu_cache(const CacheConfig& config) {
    if (validate_cache_config(config) != HAKO_PDU_ERR_OK) {
        return nullptr;
    }

    std::unique_ptr<PduCache> cache;
    switch (config.mode) {
    case CacheMode::Latest:
        cache = std::make_unique<PduLatestBuffer>();
        break;
    case CacheMode::Queue:
        cache = std::make_unique<PduLatestQueue>();
        break;
    }

    if (!cache || cache->configure(config) != HAKO_PDU_ERR_OK) {
        return nullptr;
    }
    return cache;
}

std::unique_ptr<PduCache> create_pdu_cache(const std::string& config_path) {
    CacheConfig config;
    const auto result = load_cache_config(config_path, config);
    if (result != HAKO_PDU_ERR_OK) {
        std::cerr << "PduCache Factory Error: Failed to load config file: " << config_path << std::endl;
        return nullptr;
    }
    return create_pdu_cache(config);
}

std::shared_ptr<PduComm> create_pdu_comm(const std::string& config_path) {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        std::cerr << "PduComm Factory Error: Failed to open config file: " << config_path << std::endl;
        return nullptr;
    }

    nlohmann::json config;
    try {
        ifs >> config;
        std::string protocol = config.at("protocol").get<std::string>();

        if (protocol == "tcp") {
            return std::make_shared<comm::TcpComm>();
        } else if (protocol == "udp") {
            return std::make_shared<comm::UdpComm>();
        } else if (protocol == "shm") {
#ifdef HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE
            return std::make_shared<comm::PduCommShm>();
#else
            std::cerr << "PduComm Factory Error: protocol 'shm' requested but Hakoniwa core support is disabled." << std::endl;
            return nullptr;
#endif
        } else if (protocol == "websocket") {
            return std::make_shared<comm::WebSocketComm>();
        } else if (protocol == "storage") {
            return std::make_shared<comm::StorageComm>();
        } else if (protocol == "zenoh") {
#ifdef HAKO_PDU_ENDPOINT_HAS_ZENOH
            return std::make_shared<comm::ZenohComm>();
#else
            std::cerr << "PduComm Factory Error: protocol 'zenoh' requested but Zenoh support is disabled." << std::endl;
            return nullptr;
#endif
        } else if (protocol == "rmw_zenoh") {
#ifdef HAKO_PDU_ENDPOINT_HAS_ZENOH
            return std::make_shared<comm::RmwZenohComm>();
#else
            std::cerr << "PduComm Factory Error: protocol 'rmw_zenoh' requested but Zenoh support is disabled." << std::endl;
            return nullptr;
#endif
        } else if (protocol == "mqtt") {
#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
            return std::make_shared<comm::MqttComm>();
#else
            std::cerr << "PduComm Factory Error: protocol 'mqtt' requested but MQTT support is disabled." << std::endl;
            return nullptr;
#endif
        } else {
            std::cerr << "PduComm Factory Error: Unknown protocol '" << protocol << "' in " << config_path << std::endl;
            return nullptr;
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduComm Factory Error: JSON parsing/access failed for " << config_path << ". Details: " << e.what() << std::endl;
        return nullptr;
    }
}

} // namespace pdu
} // namespace hakoniwa
