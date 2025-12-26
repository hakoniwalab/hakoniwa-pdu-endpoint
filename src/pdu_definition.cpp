#include "hakoniwa/pdu/pdu_definition.hpp"
#include <fstream>
#include <iostream>

namespace hakoniwa {
namespace pdu {

bool PduDefinition::load(const std::string& pdudef_path) {
    std::ifstream ifs(pdudef_path);
    if (!ifs.is_open()) {
        std::cerr << "PduDefinition Error: Failed to open PDU definition file: " << pdudef_path << std::endl;
        return false;
    }

    nlohmann::json config;
    try {
        ifs >> config;

        for (const auto& robot_def : config.at("robots")) {
            std::string robot_name = robot_def.at("name").get<std::string>();

            // Helper lambda to parse PDU arrays (readers and writers)
            auto parse_pdu_list = [&](const nlohmann::json& pdu_list) {
                for (const auto& pdu_def_json : pdu_list) {
                    PduDef def;
                    def.type = pdu_def_json.at("type").get<std::string>();
                    def.org_name = pdu_def_json.at("org_name").get<std::string>();
                    def.channel_id = pdu_def_json.at("channel_id").get<HakoPduChannelIdType>();
                    def.pdu_size = pdu_def_json.at("pdu_size").get<size_t>();

                    // Store the definition in the nested map
                    pdu_definitions_[robot_name][def.org_name] = def;
                }
            };
            
            if (robot_def.contains("shm_pdu_readers")) {
                parse_pdu_list(robot_def["shm_pdu_readers"]);
            }
            if (robot_def.contains("shm_pdu_writers")) {
                // Avoid duplicates if a PDU is in both read and write lists
                const auto& writer_list = robot_def["shm_pdu_writers"];
                for (const auto& pdu_def_json : writer_list) {
                    std::string org_name = pdu_def_json.at("org_name").get<std::string>();
                    if (pdu_definitions_[robot_name].find(org_name) == pdu_definitions_[robot_name].end()) {
                        PduDef def;
                        def.type = pdu_def_json.at("type").get<std::string>();
                        def.org_name = org_name;
                        def.channel_id = pdu_def_json.at("channel_id").get<HakoPduChannelIdType>();
                        def.pdu_size = pdu_def_json.at("pdu_size").get<size_t>();
                        pdu_definitions_[robot_name][def.org_name] = def;
                    }
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduDefinition Error: JSON parsing failed for " << pdudef_path << ". Details: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool PduDefinition::resolve(const std::string& robot_name, const std::string& pdu_org_name, PduDef& out_def) const {
    auto it_robot = pdu_definitions_.find(robot_name);
    if (it_robot == pdu_definitions_.end()) {
        return false; // Robot not found
    }

    auto it_pdu = it_robot->second.find(pdu_org_name);
    if (it_pdu == it_robot->second.end()) {
        return false; // PDU not found for this robot
    }

    out_def = it_pdu->second;
    return true;
}

bool PduDefinition::resolve(const std::string& robot_name, HakoPduChannelIdType channel_id, PduDef& out_def) const {
    auto it_robot = pdu_definitions_.find(robot_name);
    if (it_robot == pdu_definitions_.end()) {
        return false; // Robot not found
    }

    for (const auto& pair : it_robot->second) {
        if (pair.second.channel_id == channel_id) {
            out_def = pair.second;
            return true;
        }
    }

    return false; // PDU with that channel_id not found
}

size_t PduDefinition::get_pdu_size(const std::string& robot_name, const std::string& pdu_org_name) const {
    PduDef def;
    if (resolve(robot_name, pdu_org_name, def)) {
        return def.pdu_size;
    }
    return 0; // Not found
}

} // namespace pdu
} // namespace hakoniwa
