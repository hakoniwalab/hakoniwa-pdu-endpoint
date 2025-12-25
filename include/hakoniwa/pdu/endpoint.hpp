#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/cache/cache.hpp"
#include "hakoniwa/pdu/comm/comm.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "hakoniwa/pdu/pdu_factory.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/cache/cache.hpp"
#include "hakoniwa/pdu/comm/comm.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "hakoniwa/pdu/pdu_factory.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace hakoniwa {
namespace pdu {

class Endpoint
{
public:
    /**
     * @brief コンストラクタ
     * @param name エンドポイント名
     * @param type エンドポイントの方向（IN/OUT/INOUT）
     */
    Endpoint(const std::string& name, HakoPduEndpointDirectionType type) 
        : name_(name), type_(type) {}
    
    virtual ~Endpoint() = default;
    
    // コピー・ムーブ禁止（ポリモーフィックな基底クラス）
    Endpoint(const Endpoint&) = delete;
    Endpoint(Endpoint&&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint& operator=(Endpoint&&) = delete;
    
    /**
     * @brief エンドポイントを開く
     * @param config_path 設定ファイルのパス
     * @return エラーコード
     */
    virtual HakoPduErrorType open(const std::string& config_path) 
    {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }

        nlohmann::json config;
        try {
            ifs >> config;

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
                err = comm_->open(comm_config_path);
                if (err != HAKO_PDU_ERR_OK) {
                    return err;
                }
            }

        } catch (const nlohmann::json::exception& e) {
            // Consider logging e.what()
            return HAKO_PDU_ERR_INVALID_JSON;
        }

        if (comm_) {
            (void)comm_->set_on_recv_callback([this](const PduResolvedKey& pdu_key, std::span<const std::byte> data) {
                this->recv_callback_(pdu_key, data);
            });
        }
        return HAKO_PDU_ERR_OK;
    }
    
    /**
     * @brief エンドポイントを閉じる
     * @return エラーコード
     */
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
    
    /**
     * @brief 通信を開始する
     * @return エラーコード
     */
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
    
    /**
     * @brief 通信を停止する
     * @return エラーコード
     */
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
    
    /**
     * @brief 通信が実行中かどうかを確認する
     * @param[out] running 実行中ならtrue、そうでなければfalse
     * @return エラーコード
     */
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
        } else {
            comm_running = true; // No comm module, so it doesn't prevent running status
        }

        running = cache_running && comm_running;
        return HAKO_PDU_ERR_OK;
    }
    
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
     * @brief エンドポイント名を取得
     * @return エンドポイント名
     */
    const std::string& get_name() const { return name_; }
    
    /**
     * @brief エンドポイントの方向タイプを取得
     * @return 方向タイプ
     */
    HakoPduEndpointDirectionType get_type() const { return type_; }


protected:
    std::string                     name_;
    HakoPduEndpointDirectionType    type_;
    std::unique_ptr<PduCache>       cache_;
    std::unique_ptr<PduComm>        comm_;

private:
    //recv_callback_
    void recv_callback_(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept {
        (void)cache_->write(pdu_key, data);
    }

};

} // namespace pdu
} // namespace hakoniwa