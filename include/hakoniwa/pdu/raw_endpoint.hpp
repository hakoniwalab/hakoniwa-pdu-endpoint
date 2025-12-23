// endpoint.hpp
#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <string>
#include <span>
#include <cstddef>

namespace hakoniwa {
namespace pdu
{

/**
 * @brief 通信エンドポイントの抽象インターフェース
 * 
 * 各プロトコル実装はこのインターフェースを継承して
 * 具体的な通信処理を実装します。
 */
class RawEndpoint
{
public:
    /**
     * @brief コンストラクタ
     * @param name エンドポイント名
     * @param type エンドポイントの方向（IN/OUT/INOUT）
     */
    RawEndpoint(const std::string& name, HakoPduEndpointDirectionType type) 
        : name_(name), type_(type) {}
    
    virtual ~RawEndpoint() = default;
    
    // コピー・ムーブ禁止（ポリモーフィックな基底クラス）
    RawEndpoint(const RawEndpoint&) = delete;
    RawEndpoint(RawEndpoint&&) = delete;
    RawEndpoint& operator=(const RawEndpoint&) = delete;
    RawEndpoint& operator=(RawEndpoint&&) = delete;
    
    /**
     * @brief エンドポイントを開く
     * @param config_path 設定ファイルのパス
     * @return エラーコード
     */
    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    
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
    
    /**
     * @brief データを送信する
     * @param data 送信するデータへのポインタ
     * @param size 送信するデータのサイズ（バイト）
     * @return エラーコード
     * @note DIRECTION_OUTまたはDIRECTION_INOUTのエンドポイントで使用
     */
    virtual HakoPduErrorType send(const void* data, size_t size) noexcept = 0;
    
    /**
     * @brief データを受信する
     * @param data 受信バッファへのポインタ
     * @param buffer_size 受信バッファのサイズ（バイト）
     * @param[out] received_size 実際に受信したデータのサイズ（バイト）
     * @return エラーコード
     * @note DIRECTION_INまたはDIRECTION_INOUTのエンドポイントで使用
     */
    virtual HakoPduErrorType recv(void* data, size_t buffer_size, size_t& received_size) noexcept = 0;
    
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

    HakoPduErrorType send(std::span<const std::byte> bytes) noexcept {
        return send(bytes.data(), bytes.size());
    }

    HakoPduErrorType recv(std::span<std::byte> buf, size_t& received_size) noexcept {
        return recv(buf.data(), buf.size(), received_size);
    }

protected:
    std::string                  name_;
    HakoPduEndpointDirectionType type_;
};

}} // namespace hakoniwa::pdu