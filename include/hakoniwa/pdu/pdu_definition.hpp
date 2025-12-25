#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hako_primitive_types.h" // Added
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

namespace hakoniwa {
namespace pdu {

// Represents the definition of a single PDU channel.
struct PduDef {
    std::string type;
    std::string org_name;
    std::string name;
    HakoPduChannelIdType channel_id;
    size_t pdu_size;
    std::string method_type;
};

// Manages the loading and resolving of PDU definitions from a JSON file.
class PduDefinition {
public:
    PduDefinition() = default;

    /**
     * @brief Loads and parses the PDU definition file.
     * @param pdudef_path Path to the pdudef.json file.
     * @return True if loading and parsing were successful, false otherwise.
     */
    bool load(const std::string& pdudef_path);

    /**
     * @brief Resolves a PDU's definition by its robot and original name.
     * @param robot_name The name of the robot.
     * @param pdu_org_name The original name of the PDU (e.g., "pos", "motor").
     * @param[out] out_def The PduDef struct to be filled if found.
     * @return True if the PDU definition was found, false otherwise.
     */
    bool resolve(const std::string& robot_name, const std::string& pdu_org_name, PduDef& out_def) const;

    /**
     * @brief Resolves a PDU's definition by its robot and channel ID.
     * @param robot_name The name of the robot.
     * @param channel_id The integer channel ID of the PDU.
     * @param[out] out_def The PduDef struct to be filled if found.
     * @return True if the PDU definition was found, false otherwise.
     */
    bool resolve(const std::string& robot_name, HakoPduChannelIdType channel_id, PduDef& out_def) const;

    /**
     * @brief Gets the PDU size for a given robot and PDU original name.
     * @param robot_name The name of the robot.
     * @param pdu_org_name The original name of the PDU.
     * @return The size of the PDU, or 0 if not found.
     */
    size_t get_pdu_size(const std::string& robot_name, const std::string& pdu_org_name) const;

//private: //TODO
    // A nested map to store PDU definitions:
    // map<robot_name, map<pdu_org_name, PduDef>>
    std::map<std::string, std::map<std::string, PduDef>> pdu_definitions_;
};

} // namespace pdu
} // namespace hakoniwa
