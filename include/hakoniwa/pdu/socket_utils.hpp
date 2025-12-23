#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <netdb.h>
#include <nlohmann/json.hpp>
#include <string>

namespace hakoniwa {
namespace pdu {

HakoPduErrorType map_errno_to_error(int error_number) noexcept;
HakoPduEndpointDirectionType parse_direction(const std::string& direction);
HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json, int socket_type, addrinfo** res);

}  // namespace pdu
}  // namespace hakoniwa
