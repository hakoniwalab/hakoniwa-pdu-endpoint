#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include "hakoniwa/pdu/socket_portability.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace hakoniwa {
namespace pdu {

struct NameResolverConfig {
    bool enabled{false};
    std::string map_path;
    bool strict{false};
};

HakoPduErrorType map_errno_to_error(int error_number) noexcept;
HakoPduEndpointDirectionType parse_direction(const std::string& direction);
HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json, int socket_type, AddressInfo** res);
HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json,
                                 int socket_type,
                                 AddressInfo** res,
                                 const NameResolverConfig* resolver,
                                 std::string* resolved_address = nullptr);
HakoPduErrorType load_name_resolver_config(const nlohmann::json& comm_json,
                                           const std::string& comm_config_path,
                                           NameResolverConfig& resolver,
                                           std::string& error_message);

}  // namespace pdu
}  // namespace hakoniwa
