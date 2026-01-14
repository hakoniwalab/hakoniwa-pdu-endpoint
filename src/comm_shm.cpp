#include "hakoniwa/pdu/comm/comm_shm.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace {
HakoPduErrorType init_impl_from_config(const nlohmann::json& shm_config,
    const std::shared_ptr<PduDefinition>& pdu_def,
    std::unique_ptr<PduCommShmImp>& impl)
{
    if (impl) {
        return HAKO_PDU_ERR_OK;
    }
    if (!pdu_def) {
        std::cerr << "PduCommShm Error: PDU definition is not set." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!shm_config.contains("impl_type")) {
        std::cerr << "PduCommShm Error: 'impl_type' not specified in config." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    const std::string impl_type = shm_config.at("impl_type").get<std::string>();
    if (impl_type == "callback") {
        impl = std::make_unique<PduCommShmCallbackImpl>(pdu_def);
        return HAKO_PDU_ERR_OK;
    }
    if (impl_type == "poll") {
        if (!shm_config.contains("asset_name")) {
            std::cerr << "PduCommShm Error: 'asset_name' not specified for poll implementation." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        const std::string asset_name = shm_config.at("asset_name").get<std::string>();
        impl = std::make_unique<PduCommShmPollImpl>(pdu_def, asset_name);
        return HAKO_PDU_ERR_OK;
    }

    std::cerr << "PduCommShm Error: Unknown impl_type '" << impl_type << "' in config." << std::endl;
    return HAKO_PDU_ERR_INVALID_CONFIG;
}
} // namespace

// Initialize static members
std::map<int, PduCommShm*> PduCommShm::event_id_to_instance_map_;
std::mutex PduCommShm::event_map_mutex_;

PduCommShm::PduCommShm() : running_(false), recv_events_registered_(false) {
    // Constructor
}

PduCommShm::~PduCommShm() {
    // Destructor
    stop();
    close();
}

HakoPduErrorType PduCommShm::create_pdu_lchannels(const std::string& config_path) {
    if (!pdu_def_) { // Access inherited member
        std::cerr << "PduCommShm Error: PDU definition is not set." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        std::cerr << "PduCommShm Error: Failed to open config file: " << config_path << std::endl;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    
    nlohmann::json shm_config;
    try {
        ifs >> shm_config;
        if (!shm_config.contains("protocol") || shm_config.at("protocol").get<std::string>() != "shm") {
            std::cerr << "PduCommShm Error: protocol is not 'shm'." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        HakoPduErrorType init_err = init_impl_from_config(shm_config, pdu_def_, impl_);
        if (init_err != HAKO_PDU_ERR_OK) {
            return init_err;
        }
        if (!shm_config.contains("io") || !shm_config.at("io").contains("robots")) {
            std::cerr << "PduCommShm Error: 'io.robots' not specified in config." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        for (const auto& robot_def : shm_config.at("io").at("robots")) {
            std::string robot_name = robot_def.at("name").get<std::string>();
            for (const auto& pdu_entry : robot_def.at("pdu")) {
                std::string pdu_name = pdu_entry.at("name").get<std::string>();
                
                PduDef def;
                if (!pdu_def_->resolve(robot_name, pdu_name, def)) { // Access inherited member
                    std::cerr << "PduCommShm Error: Failed to resolve PDU '" << pdu_name << "' for robot '" << robot_name << "'" << std::endl;
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                if (impl_->create_pdu_lchannel(robot_name, def.channel_id, def.pdu_size) != HAKO_PDU_ERR_OK) {
                    std::cerr << "PduCommShm Error: Failed to create PDU channel for " << robot_name << "/" << pdu_name << std::endl;
                    return HAKO_PDU_ERR_IO_ERROR;
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduCommShm Error: JSON parsing failed for " << config_path << ". Details: " << e.what() << std::endl;
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::open(const std::string& config_path) {
    if (!pdu_def_) { // Access inherited member
        std::cerr << "PduCommShm Error: PDU definition is not set." << std::endl;
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        std::cerr << "PduCommShm Error: Failed to open config file: " << config_path << std::endl;
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    
    nlohmann::json shm_config;
    try {
        ifs >> shm_config;
        if (!shm_config.contains("protocol") || shm_config.at("protocol").get<std::string>() != "shm") {
            std::cerr << "PduCommShm Error: protocol is not 'shm'." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        HakoPduErrorType init_err = init_impl_from_config(shm_config, pdu_def_, impl_);
        if (init_err != HAKO_PDU_ERR_OK) {
            return init_err;
        }
        
        if (!shm_config.contains("io") || !shm_config.at("io").contains("robots")) {
            std::cerr << "PduCommShm Error: 'io.robots' not specified in config." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        recv_notify_keys_.clear();
        recv_events_registered_ = false;
        for (const auto& robot_def : shm_config.at("io").at("robots")) {
            std::string robot_name = robot_def.at("name").get<std::string>();
            for (const auto& pdu_entry : robot_def.at("pdu")) {
                if (pdu_entry.at("notify_on_recv").get<bool>()) {
                    std::string pdu_name = pdu_entry.at("name").get<std::string>();
                    
                    PduDef def;
                    if (!pdu_def_->resolve(robot_name, pdu_name, def)) { // Access inherited member
                        std::cerr << "PduCommShm Error: Failed to resolve PDU '" << pdu_name << "' for robot '" << robot_name << "'" << std::endl;
                        return HAKO_PDU_ERR_INVALID_CONFIG;
                    }
                    recv_notify_keys_.push_back({ robot_name, def.channel_id });
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "PduCommShm Error: JSON parsing failed for " << config_path << ". Details: " << e.what() << std::endl;
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::close() noexcept {
    // Unregister events and clean up the map
    std::lock_guard<std::mutex> lock(event_map_mutex_);
    for (int event_id : registered_event_ids_) {
        // hako_asset doesn't seem to have an unregister function, 
        // so we just clean up our internal maps.
        event_id_to_instance_map_.erase(event_id);
    }
    registered_event_ids_.clear();
    event_id_to_key_map_.clear();
    recv_notify_keys_.clear();
    recv_events_registered_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::start() noexcept {
    running_.store(true);
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::post_start() noexcept {
    if (!recv_events_registered_ && !recv_notify_keys_.empty()) {
        std::vector<int> newly_registered_ids;
        for (const auto& key : recv_notify_keys_) {
            int event_id = -1;
            if (impl_->register_rcv_event(key, PduCommShm::shm_recv_callback, event_id) != 0) {
                std::cerr << "PduCommShm Error: Failed to register recv event for " << key.robot << "/" << key.channel_id << std::endl;
                std::lock_guard<std::mutex> lock(event_map_mutex_);
                for (int registered_id : newly_registered_ids) {
                    event_id_to_instance_map_.erase(registered_id);
                    event_id_to_key_map_.erase(registered_id);
                }
                registered_event_ids_.clear();
                running_.store(false);
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            event_id_to_key_map_[event_id] = key;
            registered_event_ids_.push_back(event_id);
            newly_registered_ids.push_back(event_id);

            std::lock_guard<std::mutex> lock(event_map_mutex_);
            event_id_to_instance_map_[event_id] = this;
        }
        recv_events_registered_ = true;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::stop() noexcept {
    running_.store(false);
    // Nothing else to do for SHM
    return HAKO_PDU_ERR_OK;
}

void PduCommShm::process_recv_events() noexcept
{
    if (!running_.load()) {
        return;
    }
    if (impl_) {
        impl_->process_recv_events();
    }
}

HakoPduErrorType PduCommShm::is_running(bool& running) noexcept {
    running = running_.load();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept {
    if (native_send(pdu_key, data) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept {

    PduDef def;
    if (!pdu_def_->resolve(pdu_key.robot, pdu_key.channel_id, def)) { // Access inherited member
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (data.empty()) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT; // or OK, choose policy
    }    
    const size_t read_size = std::min(data.size(), def.pdu_size);
    data = data.subspan(0, read_size);
    if (native_recv(pdu_key, data, received_size) == 0) {
        received_size = read_size;
        return HAKO_PDU_ERR_OK;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}

void PduCommShm::shm_recv_callback(int recv_event_id) {
    std::lock_guard<std::mutex> lock(event_map_mutex_);
    if (event_id_to_instance_map_.count(recv_event_id)) {
        PduCommShm* instance = event_id_to_instance_map_[recv_event_id];
        instance->handle_shm_recv(recv_event_id);
    }
}

void PduCommShm::handle_shm_recv(int recv_event_id) {
    if (!running_.load() || !on_recv_callback_ || !pdu_def_) { // Access inherited member
        return;
    }

    PduResolvedKey key{};
    {
        std::lock_guard<std::mutex> lock(event_map_mutex_);
        auto it = event_id_to_key_map_.find(recv_event_id);
        if (it == event_id_to_key_map_.end()) {
            return; // Not found
        }
        key = it->second;
    }
    PduDef def;
    if (!pdu_def_->resolve(key.robot, key.channel_id, def)) { // Access inherited member
        std::cerr << "PduCommShm Error: Can't resolve PDU for received event. Robot: " << key.robot << " Channel: " << key.channel_id << std::endl;
        return;
    }

    std::vector<std::byte> buffer(def.pdu_size);
    size_t received_size = 0;
    if (native_recv(key, buffer, received_size) == 0) {
        on_recv_callback_(key, buffer);
    }
}
HakoPduErrorType PduCommShm::native_send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (impl_->send(pdu_key, data) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShm::native_recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (impl_->recv(pdu_key, data, received_size) == 0) {
        return HAKO_PDU_ERR_OK;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}
} // namespace comm
} // namespace pdu
} // namespace hakoniwa
