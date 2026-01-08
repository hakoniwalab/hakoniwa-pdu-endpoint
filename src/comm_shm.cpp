#include "hakoniwa/pdu/comm/comm_shm.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

// Initialize static members
std::map<int, PduCommShm*> PduCommShm::event_id_to_instance_map_;
std::mutex PduCommShm::event_map_mutex_;

PduCommShm::PduCommShm() : running_(false) {
    // Constructor
}

PduCommShm::~PduCommShm() {
    // Destructor
    stop();
    close();
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

                    int event_id = -1;
                    if (hako_asset_register_data_recv_event(robot_name.c_str(), def.channel_id, PduCommShm::shm_recv_callback, &event_id) != 0) {
                        std::cerr << "PduCommShm Error: Failed to register recv event for " << robot_name << "/" << pdu_name << std::endl;
                        return HAKO_PDU_ERR_INVALID_CONFIG;
                    }

                    PduResolvedKey key = { robot_name, def.channel_id };
                    event_id_to_key_map_[event_id] = key;
                    registered_event_ids_.push_back(event_id);

                    std::lock_guard<std::mutex> lock(event_map_mutex_);
                    event_id_to_instance_map_[event_id] = this;
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
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::start() noexcept {
    running_.store(true);
    // Nothing else to do for SHM as events are callback-driven
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType PduCommShm::stop() noexcept {
    running_.store(false);
    // Nothing else to do for SHM
    return HAKO_PDU_ERR_OK;
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
    if (hako_asset_pdu_write(pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<const char*>(data.data()), data.size()) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}
HakoPduErrorType PduCommShm::native_recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (hako_asset_pdu_read(pdu_key.robot.c_str(), pdu_key.channel_id, reinterpret_cast<char*>(data.data()), data.size()) == 0) {
        received_size = data.size();
        return HAKO_PDU_ERR_OK;
    }
    return HAKO_PDU_ERR_IO_ERROR;
}
} // namespace comm
} // namespace pdu
} // namespace hakoniwa
