#include "hakoniwa/pdu/endpoint_container.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace hakoniwa::pdu {
static constexpr HakoPduEndpointDirectionType kDefaultDir =
    HakoPduEndpointDirectionType::HAKO_PDU_ENDPOINT_DIRECTION_INOUT;

static fs::path resolve_under_base(const fs::path& base_dir, const std::string& maybe_rel)
{
    fs::path p(maybe_rel);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (base_dir / p).lexically_normal();
}

static HakoPduEndpointDirectionType parse_direction_or_default(
    const nlohmann::json& ep,
    HakoPduEndpointDirectionType default_dir)
{
    if (!ep.contains("direction") || ep["direction"].is_null()) {
        return default_dir;
    }
    if (ep["direction"].is_string()) {
        const std::string s = ep["direction"].get<std::string>();
        if (s == "in") {
            return HakoPduEndpointDirectionType::HAKO_PDU_ENDPOINT_DIRECTION_IN;
        }
        else if (s == "out") {
            return HakoPduEndpointDirectionType::HAKO_PDU_ENDPOINT_DIRECTION_OUT;
        }
        else if (s == "inout") {
            return HakoPduEndpointDirectionType::HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
        }
        else {
            return default_dir;
        }
    }
    return default_dir;
}

EndpointContainer::EndpointContainer(std::string node_id, std::string container_config_path)
    : node_id_(std::move(node_id))
    , container_config_path_(std::move(container_config_path))
{
}

HakoPduErrorType EndpointContainer::create_pdu_lchannels()
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();
    entries_.clear();

    std::ifstream ifs(container_config_path_);
    if (!ifs.is_open()) {
        last_error_ = "Failed to open container config: " + container_config_path_;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const nlohmann::json::exception& e) {
        last_error_ = std::string("Invalid JSON: ") + e.what();
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    if (!root.is_array()) {
        last_error_ = "Missing or invalid 'endpoints' array in container config.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    const nlohmann::json* founded_entry = nullptr;
    for (auto& entry: root) {
        if (!entry.is_object()) {
            last_error_ = "Invalid endpoint entry (not an object).";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!entry.contains("nodeId") || !entry["endpoints"].is_array()) {
            last_error_ = "Endpoint entry missing 'nodeId' or 'endpoints' array.";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (entry["nodeId"] != node_id_) {
            continue;
        }
        founded_entry = &entry;
        break;
    }
    if (founded_entry == nullptr) {
        last_error_ = "No endpoint entry found for nodeId: " + node_id_;
        return HAKO_PDU_ERR_NO_ENTRY;
    }

    fs::path cfg_path(container_config_path_);
    fs::path base_dir = cfg_path.parent_path();

    const HakoPduEndpointDirectionType default_dir = kDefaultDir;

    for (const auto& ep : (*founded_entry)["endpoints"]) {
        if (!ep.is_object()) {
            last_error_ = "Invalid endpoint entry (not an object).";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!ep.contains("id") || !ep["id"].is_string()) {
            last_error_ = "Endpoint entry missing string field 'id'.";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!ep.contains("config_path") || !ep["config_path"].is_string()) {
            last_error_ = "Endpoint entry missing string field 'config_path'. id=" + ep.value("id", "");
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        EndpointEntry e;
        e.id = ep["id"].get<std::string>();

        // Resolve config_path relative to container config base dir
        const std::string rel = ep["config_path"].get<std::string>();
        e.config_path = resolve_under_base(base_dir, rel).string();

        // Optional: direction
        // If present and parseable, store it; else keep std::nullopt and resolve later.
        if (ep.contains("direction") && !ep["direction"].is_null()) {
            // For now: accept int or string; store parsed final enum.
            e.direction = parse_direction_or_default(ep, default_dir);
        }

        // Optional: mode (kept but not interpreted by container)
        if (ep.contains("mode") && ep["mode"].is_string()) {
            e.mode = ep["mode"].get<std::string>();
        }

        entries_.push_back(std::move(e));
    }

    //open endpoints here
    for (const auto& e : entries_) {
        auto ep = create_(e);
        if (!ep) {
            // rollback: close already-opened endpoints
            for (auto& [id, opened_ep] : cache_) {
                if (opened_ep) { (void)opened_ep->close(); }
            }
            cache_.clear();
            started_.clear();
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        ep->create_pdu_lchannels(e.config_path);
    }
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType EndpointContainer::initialize()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (initialized_) {
        last_error_ = "EndpointContainer is already initialized.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    last_error_.clear();
    entries_.clear();

    std::ifstream ifs(container_config_path_);
    if (!ifs.is_open()) {
        last_error_ = "Failed to open container config: " + container_config_path_;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const nlohmann::json::exception& e) {
        last_error_ = std::string("Invalid JSON: ") + e.what();
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    if (!root.is_array()) {
        last_error_ = "Missing or invalid 'endpoints' array in container config.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    const nlohmann::json* founded_entry = nullptr;
    for (auto& entry: root) {
        if (!entry.is_object()) {
            last_error_ = "Invalid endpoint entry (not an object).";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!entry.contains("nodeId") || !entry["endpoints"].is_array()) {
            last_error_ = "Endpoint entry missing 'nodeId' or 'endpoints' array.";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (entry["nodeId"] != node_id_) {
            continue;
        }
        founded_entry = &entry;
        break;
    }
    if (founded_entry == nullptr) {
        last_error_ = "No endpoint entry found for nodeId: " + node_id_;
        return HAKO_PDU_ERR_NO_ENTRY;
    }

    fs::path cfg_path(container_config_path_);
    fs::path base_dir = cfg_path.parent_path();

    const HakoPduEndpointDirectionType default_dir = kDefaultDir;

    for (const auto& ep : (*founded_entry)["endpoints"]) {
        if (!ep.is_object()) {
            last_error_ = "Invalid endpoint entry (not an object).";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!ep.contains("id") || !ep["id"].is_string()) {
            last_error_ = "Endpoint entry missing string field 'id'.";
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!ep.contains("config_path") || !ep["config_path"].is_string()) {
            last_error_ = "Endpoint entry missing string field 'config_path'. id=" + ep.value("id", "");
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        EndpointEntry e;
        e.id = ep["id"].get<std::string>();

        // Resolve config_path relative to container config base dir
        const std::string rel = ep["config_path"].get<std::string>();
        e.config_path = resolve_under_base(base_dir, rel).string();

        // Optional: direction
        // If present and parseable, store it; else keep std::nullopt and resolve later.
        if (ep.contains("direction") && !ep["direction"].is_null()) {
            // For now: accept int or string; store parsed final enum.
            e.direction = parse_direction_or_default(ep, default_dir);
        }

        // Optional: mode (kept but not interpreted by container)
        if (ep.contains("mode") && ep["mode"].is_string()) {
            e.mode = ep["mode"].get<std::string>();
        }

        entries_.push_back(std::move(e));
    }

    //open endpoints here
    for (const auto& e : entries_) {
        auto ep = create_and_open_(e);
        if (!ep) {
            // rollback: close already-opened endpoints
            for (auto& [id, opened_ep] : cache_) {
                if (opened_ep) { (void)opened_ep->close(); }
            }
            cache_.clear();
            started_.clear();
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
    }

    initialized_ = true;
    return HAKO_PDU_ERR_OK;
}

std::optional<EndpointEntry> EndpointContainer::find_entry_(const std::string& endpoint_id) const
{
    for (const auto& e : entries_) {
        if (e.id == endpoint_id) {
            return e;
        }
    }
    return std::nullopt;
}

std::shared_ptr<Endpoint> EndpointContainer::create_(const EndpointEntry& e)
{
    const auto default_dir = kDefaultDir;
    const auto dir = e.direction.value_or(default_dir);

    // name == id (YAGNI)
    std::shared_ptr<Endpoint> ep;
    if (cache_.find(e.id) == cache_.end()) {
        ep = std::make_shared<Endpoint>(e.id, dir);
        cache_[e.id] = ep;
    }
    else {
        ep = cache_.at(e.id);
    }
    return ep;
}

std::shared_ptr<Endpoint> EndpointContainer::create_and_open_(const EndpointEntry& e)
{
    const auto default_dir = kDefaultDir;
    const auto dir = e.direction.value_or(default_dir);

    // name == id (YAGNI)
    std::shared_ptr<Endpoint> ep = create_(e);
    HakoPduErrorType err = ep->open(e.config_path);
    if (err != HAKO_PDU_ERR_OK) {
        last_error_ = "Endpoint open failed. id=" + e.id + " config=" + e.config_path
                      + " err=" + std::to_string(static_cast<int>(err));
        return nullptr;
    }

    started_[e.id] = false;
    return ep;
}


HakoPduErrorType EndpointContainer::start_all() noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();

    if (!initialized_) {
        last_error_ = "EndpointContainer is not initialized.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    HakoPduErrorType first_err = HAKO_PDU_ERR_OK;

    for (auto& [id, ep] : cache_) {
        if (!ep) { continue; }
        if (started_[id]) { continue; }

        const HakoPduErrorType err = ep->start();
        if (err != HAKO_PDU_ERR_OK && first_err == HAKO_PDU_ERR_OK) {
            first_err = err;
            last_error_ = "start_all failed at endpoint id=" + id
                          + " err=" + std::to_string(static_cast<int>(err));
        } else if (err == HAKO_PDU_ERR_OK) {
            started_[id] = true;
        }
    }
    return first_err;
}

HakoPduErrorType EndpointContainer::stop_all() noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();

    if (!initialized_) {
        last_error_ = "EndpointContainer is not initialized.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    HakoPduErrorType first_err = HAKO_PDU_ERR_OK;

    // Stop+close all endpoints that were created.
    for (auto& [id, ep] : cache_) {
        if (!ep) { continue; }

        // stop
        const HakoPduErrorType stop_err = ep->stop();
        if (stop_err != HAKO_PDU_ERR_OK && first_err == HAKO_PDU_ERR_OK) {
            first_err = stop_err;
            last_error_ = "stop_all: stop failed at endpoint id=" + id
                          + " err=" + std::to_string(static_cast<int>(stop_err));
        }

        // close
        const HakoPduErrorType close_err = ep->close();
        if (close_err != HAKO_PDU_ERR_OK && first_err == HAKO_PDU_ERR_OK) {
            first_err = close_err;
            last_error_ = "stop_all: close failed at endpoint id=" + id
                          + " err=" + std::to_string(static_cast<int>(close_err));
        }

        started_[id] = false;
    }

    // Optional: clear cache to allow clean re-create later.
    cache_.clear();
    started_.clear();
    initialized_ = false;    

    return first_err;
}

HakoPduErrorType EndpointContainer::start(const std::string& endpoint_id) noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();

    if (!initialized_) {
        last_error_ = "EndpointContainer is not initialized.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    auto it = cache_.find(endpoint_id);
    if (it == cache_.end() || !it->second) {
        last_error_ = "start: endpoint not found in container. id=" + endpoint_id;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    if (started_[endpoint_id]) {
        return HAKO_PDU_ERR_OK;
    }

    const HakoPduErrorType err = it->second->start();
    if (err != HAKO_PDU_ERR_OK) {
        last_error_ = "start failed. id=" + endpoint_id
                      + " err=" + std::to_string(static_cast<int>(err));
        return err;
    }
    started_[endpoint_id] = true;
    return HAKO_PDU_ERR_OK;
}
bool EndpointContainer::is_running_all() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return false;
    }
    for (const auto& [id, ep] : cache_) {
        if (!ep) {
            return false;
        }
        bool running = false;
        HakoPduErrorType err = ep->is_running(running);
        if (err != HAKO_PDU_ERR_OK || !running) {
            return false;
        }
    }
    return true;
}
std::shared_ptr<Endpoint> EndpointContainer::ref(const std::string& id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();

    if (!initialized_) {
        last_error_ = "EndpointContainer is not initialized.";
        return nullptr;
    }

    auto it = cache_.find(id);
    if (it == cache_.end() || !it->second) {
        last_error_ = "ref: endpoint not found in container. id=" + id;
        return nullptr;
    }
    return it->second;
}
HakoPduErrorType EndpointContainer::stop(const std::string& endpoint_id) noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    last_error_.clear();

    if (!initialized_) {
        last_error_ = "EndpointContainer is not initialized.";
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    auto it = cache_.find(endpoint_id);
    if (it == cache_.end() || !it->second) {
        // YAGNI: stopping non-existing is treated as OK
        return HAKO_PDU_ERR_OK;
    }

    HakoPduErrorType first_err = HAKO_PDU_ERR_OK;

    const HakoPduErrorType stop_err = it->second->stop();
    if (stop_err != HAKO_PDU_ERR_OK) {
        first_err = stop_err;
        last_error_ = "stop failed. id=" + endpoint_id
                      + " err=" + std::to_string(static_cast<int>(stop_err));
    }

    const HakoPduErrorType close_err = it->second->close();
    if (close_err != HAKO_PDU_ERR_OK && first_err == HAKO_PDU_ERR_OK) {
        first_err = close_err;
        last_error_ = "stop: close failed. id=" + endpoint_id
                      + " err=" + std::to_string(static_cast<int>(close_err));
    }

    started_[endpoint_id] = false;

    // Optional: remove from cache so it can be re-created
    cache_.erase(endpoint_id);
    started_.erase(endpoint_id);

    return first_err;
}

std::vector<std::string> EndpointContainer::list_endpoint_ids() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_) {
        ids.push_back(e.id);
    }
    return ids;
}

} // namespace hakoniwa::pdu
