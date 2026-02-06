#include "hakoniwa/pdu/endpoint_comm_multiplexer.hpp"
#include "hakoniwa/pdu/comm/comm_tcp_mux.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

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

HakoPduErrorType load_mux_config(const std::string& config_path, nlohmann::json& config, fs::path& base_dir)
{
    fs::path ep_path(config_path);
    base_dir = ep_path.parent_path();
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    try {
        ifs >> config;
    } catch (const nlohmann::json::exception&) {
        return HAKO_PDU_ERR_INVALID_JSON;
    }
    return HAKO_PDU_ERR_OK;
}
} // namespace

EndpointCommMultiplexer::EndpointCommMultiplexer(const std::string& name, HakoPduEndpointDirectionType type)
    : name_(name), type_(type)
{
}

HakoPduErrorType EndpointCommMultiplexer::open(const std::string& endpoint_mux_config_path)
{
    if (comm_) {
        return HAKO_PDU_ERR_BUSY;
    }

    nlohmann::json config;
    HakoPduErrorType err = load_mux_config(endpoint_mux_config_path, config, base_dir_);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }

    if (!config.contains("cache") || config["cache"].is_null()) {
        std::cerr << "EndpointMux config error: missing cache." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!config.contains("comm") || config["comm"].is_null()) {
        std::cerr << "EndpointMux config error: missing comm." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    std::string comm_config_path = config["comm"].get<std::string>();
    auto resolved_comm_config_path = resolve_under_base(base_dir_, comm_config_path);

    comm_ = create_comm_mux_(resolved_comm_config_path.string());
    if (!comm_) {
        std::cerr << "Failed to create CommMultiplexer." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    err = comm_->open(resolved_comm_config_path.string());
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open CommMultiplexer: " << static_cast<int>(err) << std::endl;
        return err;
    }

    endpoint_config_path_ = endpoint_mux_config_path;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType EndpointCommMultiplexer::close() noexcept
{
    if (!comm_) {
        return HAKO_PDU_ERR_OK;
    }
    return comm_->close();
}

HakoPduErrorType EndpointCommMultiplexer::start() noexcept
{
    if (!comm_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    return comm_->start();
}

HakoPduErrorType EndpointCommMultiplexer::stop() noexcept
{
    if (!comm_) {
        return HAKO_PDU_ERR_OK;
    }
    return comm_->stop();
}

std::vector<std::unique_ptr<Endpoint>> EndpointCommMultiplexer::take_endpoints()
{
    std::vector<std::unique_ptr<Endpoint>> endpoints;
    if (!comm_) {
        return endpoints;
    }

    auto sessions = comm_->take_sessions();
    if (sessions.empty()) {
        return endpoints;
    }

    for (auto& session_comm : sessions) {
        auto endpoint_name = name_ + "_" + std::to_string(++endpoint_seq_);
        auto endpoint = std::make_unique<Endpoint>(endpoint_name, type_);
        endpoint->set_comm(std::move(session_comm));

        HakoPduErrorType err = endpoint->open(endpoint_config_path_);
        if (err != HAKO_PDU_ERR_OK) {
            std::cerr << "EndpointMux failed to open endpoint: " << static_cast<int>(err) << std::endl;
            continue;
        }
        err = endpoint->start();
        if (err != HAKO_PDU_ERR_OK) {
            std::cerr << "EndpointMux failed to start endpoint: " << static_cast<int>(err) << std::endl;
            (void)endpoint->close();
            continue;
        }
        (void)endpoint->post_start();

        endpoints.push_back(std::move(endpoint));
    }

    return endpoints;
}

size_t EndpointCommMultiplexer::connected_count() const noexcept
{
    if (!comm_) {
        return 0;
    }
    return comm_->connected_count();
}

size_t EndpointCommMultiplexer::expected_count() const noexcept
{
    if (!comm_) {
        return 0;
    }
    return comm_->expected_count();
}

bool EndpointCommMultiplexer::is_ready() const noexcept
{
    if (!comm_) {
        return false;
    }
    return comm_->is_ready();
}

std::unique_ptr<comm::CommMultiplexer> EndpointCommMultiplexer::create_comm_mux_(const std::string& comm_config_path)
{
    std::ifstream config_stream(comm_config_path);
    if (!config_stream) {
        return nullptr;
    }
    nlohmann::json config_json;
    try {
        config_stream >> config_json;
    } catch (const nlohmann::json::exception&) {
        return nullptr;
    }

    if (!config_json.contains("protocol")) {
        return nullptr;
    }
    const std::string protocol = config_json.at("protocol").get<std::string>();
    if (protocol == "tcp") {
        return std::make_unique<comm::TcpCommMultiplexer>();
    }
    return nullptr;
}

} // namespace pdu
} // namespace hakoniwa
