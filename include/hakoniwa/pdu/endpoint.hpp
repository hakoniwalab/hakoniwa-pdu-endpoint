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

namespace hakoniwa {
namespace pdu {

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
    
    virtual HakoPduErrorType open(const std::string& config_path) 
    {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }

        nlohmann::json config;
        try {
            ifs >> config;

            // PduDefinition is optional
            if (config.contains("pdu_def_path") && !config["pdu_def_path"].is_null()) {
                pdu_def_ = std::make_shared<PduDefinition>();
                if (!pdu_def_->load(config["pdu_def_path"].get<std::string>())) {
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
            }

            // Cache is mandatory
            if (!config.contains("cache") || config["cache"].is_null()) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            std::string cache_config_path = config["cache"].get<std::string>();

            cache_ = create_pdu_cache(cache_config_path);
            if (!cache_) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
            HakoPduErrorType err = cache_->open(cache_config_path);
            if (err != HAKO_PDU_ERR_OK) {
                return err;
            }

            // Comm is optional
            if (config.contains("comm") && !config["comm"].is_null()) {
                std::string comm_config_path = config["comm"].get<std::string>();

                comm_ = create_pdu_comm(comm_config_path);
                if (!comm_) {
                    return HAKO_PDU_ERR_INVALID_CONFIG;
                }
                // Pass PDU definition to comm module
                if (pdu_def_) {
                    comm_->set_pdu_definition(pdu_def_);
                }
                err = comm_->open(comm_config_path);
                if (err != HAKO_PDU_ERR_OK) {
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
            running = false;
            return HAKO_PDU_ERR_OK; // Or an error, since cache is mandatory
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
            return cache_->write(pdu_key, data);
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

    const std::string& get_name() const { return name_; }
    HakoPduEndpointDirectionType get_type() const { return type_; }


protected:
    std::string                     name_;
    HakoPduEndpointDirectionType    type_;
    std::shared_ptr<PduDefinition>  pdu_def_; // Changed
    std::unique_ptr<PduCache>       cache_;
    std::shared_ptr<PduComm>        comm_;

private:
    void recv_callback_(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept {
        (void)cache_->write(pdu_key, data);
    }

};

} // namespace pdu
} // namespace hakoniwa