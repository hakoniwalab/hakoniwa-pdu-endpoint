#include "hakoniwa/pdu/pdu_factory.hpp"
#include "hakoniwa/pdu/cache/cache_buffer.hpp"
#include "hakoniwa/pdu/cache/cache_queue.hpp"
#include "hakoniwa/pdu/comm/comm_tcp.hpp"
#include "hakoniwa/pdu/comm/comm_udp.hpp"
#include "hakoniwa/pdu/comm/comm_shm.hpp" // Added
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace hakoniwa {
namespace pdu {

std::unique_ptr<PduCache> create_pdu_cache(const std::string& config_path) {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        // Simple error logging, a more robust logging mechanism should be used in a real application.
        std::cerr << "PduCache Factory Error: Failed to open config file: " << config_path << std::endl;
        return nullptr;
    }

    nlohmann::json config;
    try {
        ifs >> config;
        std::string mode = config.at("store").at("mode").get<std::string>();

        if (mode == "latest") {
            return std::make_unique<PduLatestBuffer>();
        } else if (mode == "queue") {
            return std::make_unique<PduLatestQueue>();
        } else {
            std::cerr << "PduCache Factory Error: Unknown cache mode '" << mode << "' in " << config_path << std::endl;
            return nullptr;
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduCache Factory Error: JSON parsing/access failed for " << config_path << ". Details: " << e.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<PduComm> create_pdu_comm(const std::string& config_path) {
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
            return std::make_unique<comm::TcpComm>();
        } else if (protocol == "udp") {
            return std::make_unique<comm::UdpComm>();
        } else if (protocol == "shm") { // Added
            return std::make_unique<comm::PduCommShm>(); // Added
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
