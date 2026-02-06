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

/*
 * Threading assumptions:
 * - open/close/start/stop are called from a single thread (initialization/shutdown).
 * - set_on_recv_callback is configured during initialization and not changed afterward.
 * - send/recv may be called from multiple threads, but callers must serialize access if needed.
 * - Comm implementations may use background threads; close/stop can be used to interrupt blocking I/O.
 */
// Endpoint composes Cache + Comm (+ optional PDU definition) into a single API.
// Semantics are defined by explicit configuration and must not be implicit.
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

    std::shared_ptr<PduDefinition> get_pdu_definition() const{
        return pdu_def_;
    }

    // Inject comm before open(). Used by multiplexers to supply per-session comms.
    void set_comm(std::shared_ptr<PduComm> comm)
    {
        comm_ = std::move(comm);
    }

    // Optional: call before open() when the comm layer needs PDU channels created upfront.
    // open() without this call is also supported.
    // Pre-create PDU channels when required by comm (e.g., SHM). Optional.
    virtual HakoPduErrorType create_pdu_lchannels(const std::string& endpoint_config_path)
    {
        nlohmann::json config;
        fs::path base_dir;
        HakoPduErrorType err = load_endpoint_config_(endpoint_config_path, config, base_dir);
        if (err != HAKO_PDU_ERR_OK) {
            return err;
        }
        try {
            err = load_pdu_definition_if_needed_(config, base_dir, true);
            if (err != HAKO_PDU_ERR_OK) {
                return err;
            }

            // Comm is mandatory
            if (config.contains("comm") && !config["comm"].is_null()) {
                std::string comm_config_path = config["comm"].get<std::string>();
                auto resolved_comm_config_path = resolve_under_base(base_dir, comm_config_path);

                comm_ = create_pdu_comm(resolved_comm_config_path);
                if (!comm_) {
                    std::cerr << "Failed to create PDU Comm module." << std::endl;
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                // Pass PDU definition to comm module
                comm_->set_pdu_definition(pdu_def_);
                HakoPduErrorType err = comm_->create_pdu_lchannels(resolved_comm_config_path);
                if (err != HAKO_PDU_ERR_OK) {
                    std::cerr << "Failed to create_pdu_lchannels PDU Comm: " << static_cast<int>(err) << std::endl;
                    return err;
                }
            }
            else {
                std::cerr << "PDU Comm configuration is missing." << std::endl;
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
        } catch (const nlohmann::json::exception& e) {
            return HAKO_PDU_ERR_INVALID_JSON;
        }

        return HAKO_PDU_ERR_OK;
    }
    
    // Load cache/comm (and optional PDU definition) from endpoint config.
    virtual HakoPduErrorType open(const std::string& endpoint_config_path) 
    {
        nlohmann::json config;
        fs::path base_dir;
        HakoPduErrorType err = load_endpoint_config_(endpoint_config_path, config, base_dir);
        if (err != HAKO_PDU_ERR_OK) {
            return err;
        }
        try {
            err = load_pdu_definition_if_needed_(config, base_dir, false);
            if (err != HAKO_PDU_ERR_OK) {
                return err;
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

                if (!comm_) {
                    comm_ = create_pdu_comm(resolved_comm_config_path);
                }
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
    
    // Close cache/comm and release resources. Safe to call even if not started.
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
    
    // Start cache/comm processing threads if any.
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

    // Optional post-start hook (comm only).
    virtual HakoPduErrorType post_start() noexcept
    {
        std::cout << "DEBUG: Endpoint post_start called. name=" << name_ << std::endl;
        if (comm_) {
            std::cout << "DEBUG: Endpoint calling comm_->post_start(). name=" << name_ << std::endl;
            return comm_->post_start();
        }
        return HAKO_PDU_ERR_OK;
    }
    
    // Stop cache/comm processing threads if any.
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
    
    // Report whether both cache and comm are running.
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

    // Only meaningful for SHM poll implementation; other comm types are no-op.
    virtual void process_recv_events() noexcept
    {
        if (comm_) {
            comm_->process_recv_events();
        }
    }

    // High-level API using PDU names (requires pdu_def to be loaded).
    // Returns HAKO_PDU_ERR_UNSUPPORTED if pdu_def is not provided.
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
    
    // High-level recv by PDU name (requires pdu_def to be loaded).
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

    // Low-level API using resolved channel IDs (always available).
    virtual HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
    {
        if (comm_) {
            #ifdef ENABLE_DEBUG_MESSAGES
            std::cout << "DEBUG: Endpoint sending PDU: robot=" << pdu_key.robot
                      << " channel=" << pdu_key.channel_id
                      << " size=" << data.size() << std::endl;
            #endif
            return comm_->send(pdu_key, data);
        }
        else {
            auto ret = cache_->write(pdu_key, data);
            if (ret != HAKO_PDU_ERR_OK) {
                return ret;
            }
            notify_subscribers_(pdu_key, data);
            return HAKO_PDU_ERR_OK;
        }
    }
    // Low-level recv by channel ID (cache-backed).
    virtual HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept
    {
        auto errcode = cache_->read(pdu_key, data, received_size);
        if (errcode == HAKO_PDU_ERR_OK) {
            return HAKO_PDU_ERR_OK;
        }
        if (comm_) {
            return comm_->recv(pdu_key, data, received_size);
        }
        return errcode;
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
    // subscribe_on_recv_callback should be called during initialization (before start()).
    void subscribe_on_recv_callback(const PduResolvedKey& pdu_key, OnRecvCallback cb) noexcept
    {
        std::cout << "DEBUG: subscribe_on_recv_callback: endpoint=" << name_
                  << " robot=" << pdu_key.robot
                  << " channel=" << pdu_key.channel_id
                  << std::endl;
        std::lock_guard<std::mutex> lock(cb_mtx_);
        per_pdu_callbacks_.emplace_back(pdu_key, std::move(cb));
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
    std::vector<std::pair<PduResolvedKey, OnRecvCallback>> per_pdu_callbacks_;
    void notify_subscribers_(const PduResolvedKey& pdu_key,
                            std::span<const std::byte> data) noexcept
    {
        //std::cout << "DEBUG: notify_subscribers_ called for Robot: " << pdu_key.robot << " Channel ID: " << pdu_key.channel_id << std::endl;
        bool is_found = false;
        std::vector<OnRecvCallback> targets;
        {
            std::lock_guard<std::mutex> lock(cb_mtx_);
            for (const auto& [sub_key, cb] : per_pdu_callbacks_) {
                //std::cout << "DEBUG: Checking subscriber for Robot: " << sub_key.robot << " Channel ID: " << sub_key.channel_id << std::endl;
                if (sub_key.robot == pdu_key.robot &&
                    sub_key.channel_id == pdu_key.channel_id) {
                    //std::cout << "DEBUG: Found matching subscriber for Robot: " << pdu_key.robot << " Channel ID: " << pdu_key.channel_id << std::endl;
                    targets.push_back(cb); // copy
                    is_found = true;
                }
            }
        }
        if (!is_found) {
            std::cerr << "WARNING: No subscribers found for Robot: " << pdu_key.robot << " Channel ID: " << pdu_key.channel_id << std::endl;
            return;
        }
        for (auto& cb : targets) {
            //std::cout << "DEBUG: Invoking subscriber callback for Robot: " << pdu_key.robot << " Channel ID: " << pdu_key.channel_id << std::endl;
            cb(pdu_key, data);
        }
    }

    /*
     * call from comm when data is received
     */
    void recv_callback_(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept
    {
        //std::cout << "DEBUG: Endpoint recv_callback_ called for Robot: " << pdu_key.robot << " Channel ID: " << pdu_key.channel_id << std::endl;
        if (!cache_) { 
            std::cerr << "PDU Cache module is not initialized in recv_callback_. Ignoring received data." << std::endl;
            return; 
        }
        (void)cache_->write(pdu_key, data);

        notify_subscribers_(pdu_key, data);
    }
    fs::path resolve_under_base(const fs::path& base_dir, const std::string& maybe_rel)
    {
        fs::path p(maybe_rel);
        if (p.is_absolute()) {
            return p.lexically_normal();
        }
        return (base_dir / p).lexically_normal();
    }
    HakoPduErrorType load_endpoint_config_(const std::string& endpoint_config_path,
        nlohmann::json& config,
        fs::path& base_dir)
    {
        fs::path ep_path(endpoint_config_path);
        base_dir = ep_path.parent_path();
        std::ifstream ifs(endpoint_config_path);
        if (!ifs.is_open()) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }
        try {
            ifs >> config;
        } catch (const nlohmann::json::exception& e) {
            return HAKO_PDU_ERR_INVALID_JSON;
        }
        return HAKO_PDU_ERR_OK;
    }
    HakoPduErrorType load_pdu_definition_if_needed_(const nlohmann::json& config,
        const fs::path& base_dir,
        bool required)
    {
        if (config.contains("pdu_def_path") && !config["pdu_def_path"].is_null()) {
            if (!pdu_def_) {
                pdu_def_ = std::make_shared<PduDefinition>();
                auto resolved_pdu_def_path = resolve_under_base(base_dir, config["pdu_def_path"].get<std::string>());
                std::cout << "PDU Definition: loading from " << resolved_pdu_def_path << std::endl;
                if (!pdu_def_->load(resolved_pdu_def_path)) {
                    std::cerr << "PDU Definition: failed to load from " << resolved_pdu_def_path << std::endl;
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                std::cout << "PDU Definition: loaded successfully" << std::endl;
            }
            return HAKO_PDU_ERR_OK;
        }
        if (required) {
            std::cerr << "PDU Definition path is not specified." << std::endl;
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        return HAKO_PDU_ERR_OK;
    }

};

} // namespace pdu
} // namespace hakoniwa
