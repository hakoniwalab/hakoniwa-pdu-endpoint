#include "hakoniwa/pdu/socket_utils.hpp"

#include <netdb.h>

namespace hakoniwa {
namespace pdu {

HakoPduErrorType map_errno_to_error(int error_number) noexcept
{
    if (error_number == EAGAIN || error_number == EWOULDBLOCK) {
        return HAKO_PDU_ERR_TIMEOUT;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}

HakoPduEndpointDirectionType parse_direction(const std::string& direction)
{
    if (direction == "in") {
        return HAKO_PDU_ENDPOINT_DIRECTION_IN;
    }
    if (direction == "out") {
        return HAKO_PDU_ENDPOINT_DIRECTION_OUT;
    }
    return HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
}

HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json, int socket_type, addrinfo** res)
{
    if (!endpoint_json.contains("address") || !endpoint_json.contains("port")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const std::string address = endpoint_json.at("address").get<std::string>();
    const int port = endpoint_json.at("port").get<int>();
    const std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socket_type;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(address.c_str(), port_str.c_str(), &hints, res) != 0 || *res == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa
