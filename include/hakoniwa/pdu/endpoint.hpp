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
    virtual HakoPduErrorType close() noexcept = 0;
    
    /**
     * @brief 通信を開始する
     * @return エラーコード
     */
    virtual HakoPduErrorType start() noexcept = 0;
    
    /**
     * @brief 通信を停止する
     * @return エラーコード
     */
    virtual HakoPduErrorType stop() noexcept = 0;
    
    /**
     * @brief 通信が実行中かどうかを確認する
     * @param[out] running 実行中ならtrue、そうでなければfalse
     * @return エラーコード
     */
    virtual HakoPduErrorType is_running(bool& running) noexcept = 0;
    
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

}
}
