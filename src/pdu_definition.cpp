#include "hakoniwa/pdu/pdu_definition.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace hakoniwa {
namespace pdu {

namespace {
namespace fs = std::filesystem;

fs::path resolve_under_base(const fs::path& base_dir, const std::string& maybe_rel)
{
    fs::path p(maybe_rel);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (base_dir / p).lexically_normal();
}
} // namespace

bool PduDefinition::load(const std::string& pdudef_path) {
    std::ifstream ifs(pdudef_path);
    if (!ifs.is_open()) {
        std::cerr << "PduDefinition Error: Failed to open PDU definition file: " << pdudef_path << std::endl;
        return false;
    }

    nlohmann::json config;
    try {
        ifs >> config;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduDefinition Error: JSON parsing failed for " << pdudef_path << ". Details: " << e.what() << std::endl;
        return false;
    }

    try {
        if (config.contains("paths")) {
            fs::path base_dir = fs::path(pdudef_path).parent_path();
            return load_compact_(config, base_dir);
        }
        return load_legacy_(config);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduDefinition Error: JSON access failed for " << pdudef_path << ". Details: " << e.what() << std::endl;
        return false;
    }
}

bool PduDefinition::load_legacy_(const nlohmann::json& config) {
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

    return true;
}

bool PduDefinition::load_compact_(const nlohmann::json& config, const std::filesystem::path& base_dir) {
    std::map<std::string, std::vector<PduDef>> pdutype_sets;

    for (const auto& entry : config.at("paths")) {
        const std::string set_id = entry.at("id").get<std::string>();
        const std::string raw_path = entry.at("path").get<std::string>();
        const fs::path resolved = resolve_under_base(base_dir, raw_path);

        std::ifstream ifs(resolved);
        if (!ifs.is_open()) {
            std::cerr << "PduDefinition Error: Failed to open PDU types file: " << resolved.string() << std::endl;
            return false;
        }

        nlohmann::json pdutypes;
        try {
            ifs >> pdutypes;
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "PduDefinition Error: JSON parsing failed for " << resolved.string() << ". Details: " << e.what() << std::endl;
            return false;
        }

        if (!pdutypes.is_array()) {
            std::cerr << "PduDefinition Error: PDU types file must be an array: " << resolved.string() << std::endl;
            return false;
        }

        std::vector<PduDef> defs;
        for (const auto& pdu_def_json : pdutypes) {
            PduDef def;
            def.channel_id = pdu_def_json.at("channel_id").get<HakoPduChannelIdType>();
            def.pdu_size = pdu_def_json.at("pdu_size").get<size_t>();
            def.org_name = pdu_def_json.at("name").get<std::string>();
            def.name = def.org_name;
            def.type = pdu_def_json.at("type").get<std::string>();
            defs.push_back(def);
        }

        pdutype_sets.emplace(set_id, std::move(defs));
    }

    for (const auto& robot_def : config.at("robots")) {
        const std::string robot_name = robot_def.at("name").get<std::string>();
        const std::string set_id = robot_def.at("pdutypes_id").get<std::string>();

        auto it = pdutype_sets.find(set_id);
        if (it == pdutype_sets.end()) {
            std::cerr << "PduDefinition Error: PDU types id not found: " << set_id << std::endl;
            return false;
        }

        for (const auto& def : it->second) {
            pdu_definitions_[robot_name][def.org_name] = def;
        }
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

HakoPduChannelIdType PduDefinition::get_pdu_channel_id(const std::string& robot_name, const std::string& pdu_org_name) const {
    PduDef def;
    if (resolve(robot_name, pdu_org_name, def)) {
        return def.channel_id;
    }
    return -1; // Not found
}

} // namespace pdu
} // namespace hakoniwa
