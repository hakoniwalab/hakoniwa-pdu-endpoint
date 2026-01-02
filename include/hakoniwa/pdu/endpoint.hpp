#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/pdu_definition.hpp" // Added
#include "hakoniwa/pdu/cache/cache.hpp"
#include "hakoniwa/pdu/comm/comm.hpp"
#include "hakoniwa/pdu/pdu_factory.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <iostream>

namespace fs = std::filesystem;

namespace hakoniwa {
namespace pdu {
using OnRecvCallback = std::function<void(const PduResolvedKey&, std::span<const std::byte>)>;

class Endpoint
{
public:
    Endpoint(const std::string& name, HakoPduEndpointDirectionType type) 
        : name_(name), type_(type) {}
    
    virtual ~Endpoint() = default;
    
    Endpoint(const Endpoint&) = delete;
    Endpoint(Endpoint&&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint& operator=(Endpoint&&) = delete;

    PduDefinition& get_pdu_definition() {
        return *pdu_def_;
    }
    
    virtual HakoPduErrorType open(const std::string& endpoint_config_path) 
    {
        fs::path ep_path(endpoint_config_path);
        fs::path base_dir = ep_path.parent_path();

        std::ifstream ifs(endpoint_config_path);
        if (!ifs.is_open()) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }
        nlohmann::json config;
        try {
            ifs >> config;

            // PduDefinition is optional
            if (config.contains("pdu_def_path") && !config["pdu_def_path"].is_null()) {
                pdu_def_ = std::make_shared<PduDefinition>();
                auto resolved_pdu_def_path = resolve_under_base(base_dir, config["pdu_def_path"].get<std::string>());
                if (!pdu_def_->load(resolved_pdu_def_path)) {
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
            }

            // Cache is mandatory
            if (!config.contains("cache") || config["cache"].is_null()) {
                std::cerr << "PDU Cache configuration is missing." << std::endl;
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            std::string cache_config_path = config["cache"].get<std::string>();
            auto resolved_cache_config_path = resolve_under_base(base_dir, cache_config_path);

            cache_ = create_pdu_cache(resolved_cache_config_path);
            if (!cache_) {
                std::cerr << "Failed to create PDU Cache module: " << resolved_cache_config_path << std::endl;
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            HakoPduErrorType err = cache_->open(resolved_cache_config_path);
            if (err != HAKO_PDU_ERR_OK) {
                std::cerr << "Failed to open PDU Cache: " << static_cast<int>(err) << std::endl;
                return err;
            }
            // Comm is optional
            if (config.contains("comm") && !config["comm"].is_null()) {
                std::string comm_config_path = config["comm"].get<std::string>();
                auto resolved_comm_config_path = resolve_under_base(base_dir, comm_config_path);

                comm_ = create_pdu_comm(resolved_comm_config_path);
                if (!comm_) {
                    std::cerr << "Failed to create PDU Comm module." << std::endl;
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                // Pass PDU definition to comm module
                if (pdu_def_) {
                    comm_->set_pdu_definition(pdu_def_);
                }
                err = comm_->open(resolved_comm_config_path);
                if (err != HAKO_PDU_ERR_OK) {
                    std::cerr << "Failed to open PDU Comm: " << static_cast<int>(err) << std::endl;
                    return err;
                }
            }

        } catch (const nlohmann::json::exception& e) {
            return HAKO_PDU_ERR_INVALID_JSON;
        }

        if (comm_) {
            (void)comm_->set_on_recv_callback([this](const PduResolvedKey& pdu_key, std::span<const std::byte> data) {
                this->recv_callback_(pdu_key, data);
            });
        }
        return HAKO_PDU_ERR_OK;
    }
    
    virtual HakoPduErrorType close() noexcept
    {
        HakoPduErrorType err = HAKO_PDU_ERR_OK;
        if (comm_) {
            (void)comm_->set_on_recv_callback(nullptr);
            err = comm_->close();
        }
        if (cache_) {
            HakoPduErrorType cache_err = cache_->close();
            if (err == HAKO_PDU_ERR_OK) {
                err = cache_err;
            }
        }
        return err;
    }
    
    virtual HakoPduErrorType start() noexcept
    {
        if (cache_) {
            HakoPduErrorType err = cache_->start();
            if (err != HAKO_PDU_ERR_OK) return err;
        }
        if (comm_) {
            return comm_->start();
        }
        return HAKO_PDU_ERR_OK;
    }
    
    virtual HakoPduErrorType stop() noexcept
    {
        HakoPduErrorType err = HAKO_PDU_ERR_OK;
        if (comm_) {
            err = comm_->stop();
        }
        if (cache_) {
            HakoPduErrorType cache_err = cache_->stop();
            if (err == HAKO_PDU_ERR_OK) {
                err = cache_err;
            }
        }
        return err;
    }
    
    virtual HakoPduErrorType is_running(bool& running) noexcept
    {        
        bool cache_running = false;
        if (cache_) {
            HakoPduErrorType err = cache_->is_running(cache_running);
            if (err != HAKO_PDU_ERR_OK) {
                running = false;
                return err;
            }
        } else {
            std::cerr << "PDU Cache module is not initialized. name=" << name_ << std::endl;
            return HAKO_PDU_ERR_INVALID_JSON;
        }

        bool comm_running = false;
        if (comm_) {
            HakoPduErrorType err = comm_->is_running(comm_running);
            if (err != HAKO_PDU_ERR_OK) {
                running = false;
                return err;
            }
        }
        else {
            comm_running = true; // No comm module, so it doesn't prevent running status
        }

        running = cache_running && comm_running;
        return HAKO_PDU_ERR_OK;
    }

    // High-level API using PDU names (requires pdu_def to be loaded)
    virtual HakoPduErrorType send(const PduKey& pdu_key, std::span<const std::byte> data) noexcept
    {
        if (!pdu_def_) {
            return HAKO_PDU_ERR_UNSUPPORTED;
        }
        PduDef def;
        if (!pdu_def_->resolve(pdu_key.robot, pdu_key.pdu, def)) {
            return HAKO_PDU_ERR_INVALID_PDU_KEY;
        }
        PduResolvedKey resolved_key = { pdu_key.robot, def.channel_id };
        return send(resolved_key, data);
    }
    
    virtual HakoPduErrorType recv(const PduKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
    {
        if (!pdu_def_) {
            return HAKO_PDU_ERR_UNSUPPORTED;
        }
        PduDef def;
        if (!pdu_def_->resolve(pdu_key.robot, pdu_key.pdu, def)) {
            return HAKO_PDU_ERR_INVALID_PDU_KEY;
        }
        PduResolvedKey resolved_key = { pdu_key.robot, def.channel_id };
        return recv(resolved_key, data, received_size);
    }

    // Low-level API using resolved channel IDs
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
    {
        if (comm_) {
            return comm_->send(pdu_key, data);
        }
        else {
            auto ret = cache_->write(pdu_key, data);
            if (ret != HAKO_PDU_ERR_OK) {
                return ret;
            }
            OnRecvCallback cb;
            {
                std::lock_guard<std::mutex> lock(cb_mtx_);
                cb = on_recv_cb_;
            }
            if (cb) {
                /*
                * call user callback
                */
                cb(pdu_key, data);
            }
            return HAKO_PDU_ERR_OK;
        }
    }
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
    {
        return cache_->read(pdu_key, data, received_size);
    }

    /**
     * @brief Get the PDU size for a given PduKey.
     * @param pdu_key The name-based PDU key.
     * @return The size of the PDU, or 0 if PDU definition is not loaded or PDU is not found.
     */
    size_t get_pdu_size(const PduKey& pdu_key) const {
        if (!pdu_def_) {
            return 0; // PDU definition not loaded
        }
        return pdu_def_->get_pdu_size(pdu_key.robot, pdu_key.pdu);
    }

    /**
     * @brief Get the PDU channel ID for a given PduKey.
     * @param pdu_key The name-based PDU key.
     * @return The channel ID, or -1 if PDU definition is not loaded or PDU is not found.
     */
    HakoPduChannelIdType get_pdu_channel_id(const PduKey& pdu_key) const {
        if (!pdu_def_) {
            return -1; // PDU definition not loaded, or -1 is an invalid channel ID
        }
        return pdu_def_->get_pdu_channel_id(pdu_key.robot, pdu_key.pdu);
    }
    std::string get_pdu_name(const PduResolvedKey& pdu_key) const {
        if (!pdu_def_) {
            return ""; // PDU definition not loaded
        }
        PduDef def;
        if (pdu_def_->resolve(pdu_key.robot, pdu_key.channel_id, def)) {
            return def.org_name;
        }
        return ""; // Not found
    }
    void set_on_recv_callback(OnRecvCallback cb) noexcept
    {
        std::lock_guard<std::mutex> lock(cb_mtx_);
        on_recv_cb_ = std::move(cb);
    }
    const std::string& get_name() const { return name_; }
    HakoPduEndpointDirectionType get_type() const { return type_; }


protected:
    std::string                     name_;
    HakoPduEndpointDirectionType    type_;
    std::shared_ptr<PduDefinition>  pdu_def_; // Changed
    std::unique_ptr<PduCache>       cache_;
    std::shared_ptr<PduComm>        comm_;

private:
    mutable std::mutex cb_mtx_;
    OnRecvCallback on_recv_cb_;

    /*
     * call from comm when data is received
     */
    void recv_callback_(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
    {
        if (!cache_) { return; }
        (void)cache_->write(pdu_key, data);

        OnRecvCallback cb;
        {
            std::lock_guard<std::mutex> lock(cb_mtx_);
            cb = on_recv_cb_;
        }
        if (cb) {
            /*
             * call user callback
             */
            cb(pdu_key, data);
        }
    }
    fs::path resolve_under_base(const fs::path& base_dir, const std::string& maybe_rel)
    {
        fs::path p(maybe_rel);
        if (p.is_absolute()) {
            return p.lexically_normal();
        }
        return (base_dir / p).lexically_normal();
    }

};

} // namespace pdu
} // namespace hakoniwa