#include "hakoniwa/pdu/socket_utils.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"

#include <arpa/inet.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <optional>

namespace hakoniwa {
namespace pdu {

namespace {

bool is_ip_literal(const std::string& address)
{
    if (address.empty()) {
        return false;
    }
    sockaddr_in sa4{};
    if (inet_pton(AF_INET, address.c_str(), &sa4.sin_addr) == 1) {
        return true;
    }
    sockaddr_in6 sa6{};
    return inet_pton(AF_INET6, address.c_str(), &sa6.sin6_addr) == 1;
}

std::optional<std::string> lookup_mapped_address(const std::string& map_path, const std::string& hostname)
{
    std::ifstream ifs(map_path);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    nlohmann::json j;
    try {
        ifs >> j;
    } catch (...) {
        return std::nullopt;
    }
    if (j.is_object()) {
        auto it = j.find(hostname);
        if (it != j.end() && it->is_string()) {
            return it->get<std::string>();
        }
        auto nodes_it = j.find("nodes");
        if (nodes_it != j.end() && nodes_it->is_object()) {
            auto nit = nodes_it->find(hostname);
            if (nit != nodes_it->end() && nit->is_string()) {
                return nit->get<std::string>();
            }
        }
    }
    return std::nullopt;
}

} // namespace

HakoPduErrorType map_errno_to_error(int error_number) noexcept
{
    if (is_socket_would_block(error_number)) {
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
    return resolve_address(endpoint_json, socket_type, res, nullptr, nullptr);
}

HakoPduErrorType resolve_address(const nlohmann::json& endpoint_json,
                                 int socket_type,
                                 addrinfo** res,
                                 const NameResolverConfig* resolver,
                                 std::string* resolved_address)
{
    if (!endpoint_json.contains("address") || !endpoint_json.contains("port")) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    std::string address = endpoint_json.at("address").get<std::string>();
    const int port = endpoint_json.at("port").get<int>();
    const std::string port_str = std::to_string(port);

    if (resolver && resolver->enabled && !resolver->map_path.empty() && !is_ip_literal(address)) {
        auto mapped = lookup_mapped_address(resolver->map_path, address);
        if (mapped.has_value() && !mapped->empty()) {
            std::cout << "NameResolver mapped host: " << address << " -> " << *mapped << std::endl;
            address = *mapped;
        } else if (resolver->strict) {
            std::cerr << "NameResolver failed to resolve host in strict mode: " << address
                      << " map=" << resolver->map_path << std::endl;
            return HAKO_PDU_ERR_INVALID_ARGUMENT;
        }
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socket_type;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(address.c_str(), port_str.c_str(), &hints, res) != 0 || *res == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (resolved_address) {
        *resolved_address = address;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType load_name_resolver_config(const nlohmann::json& comm_json,
                                           const std::string& comm_config_path,
                                           NameResolverConfig& resolver,
                                           std::string& error_message)
{
    resolver = NameResolverConfig{};
    error_message.clear();
    if (!comm_json.contains("name_resolver")) {
        const char* env_map_path = std::getenv("HAKO_PDU_NAME_RESOLVER_PATH");
        if (env_map_path != nullptr && env_map_path[0] != '\0') {
            std::filesystem::path map_path = env_map_path;
            if (map_path.is_relative()) {
                map_path = std::filesystem::path(comm_config_path).parent_path() / map_path;
            }
            resolver.enabled = true;
            resolver.map_path = map_path.lexically_normal().string();
            resolver.strict = false;
        }
        return HAKO_PDU_ERR_OK;
    }
    const auto& nr = comm_json.at("name_resolver");
    if (!nr.is_object()) {
        error_message = "name_resolver must be an object";
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const std::string type = nr.value("type", std::string{"file"});
    if (type != "file") {
        error_message = "name_resolver.type must be 'file'";
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (!nr.contains("path") || !nr.at("path").is_string()) {
        error_message = "name_resolver.path must be a string";
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    std::filesystem::path map_path = nr.at("path").get<std::string>();
    if (map_path.is_relative()) {
        map_path = std::filesystem::path(comm_config_path).parent_path() / map_path;
    }
    resolver.enabled = true;
    resolver.map_path = map_path.lexically_normal().string();
    resolver.strict = nr.value("strict", false);
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa
