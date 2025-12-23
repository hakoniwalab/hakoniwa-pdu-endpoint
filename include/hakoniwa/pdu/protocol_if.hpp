#pragma once

#include "hakoniwa/pdu/endpoint.hpp"

namespace hakoniwa {
namespace pdu {

/**
 * @brief プロトコル通信の抽象インターフェース
 * 
 * このインターフェースは様々な通信プロトコルの実装基盤を提供します。
 * 実装クラスは全ての純粋仮想関数をオーバーライドする必要があります。
 */
class ProtocolIf
{
public:
    ProtocolIf() = default;
    virtual ~ProtocolIf() = default;
    
    // コピー・ムーブ禁止（インターフェースクラス）
    ProtocolIf(const ProtocolIf&) = delete;
    ProtocolIf(ProtocolIf&&) = delete;
    ProtocolIf& operator=(const ProtocolIf&) = delete;
    ProtocolIf& operator=(ProtocolIf&&) = delete;

    /**
     * @brief プロトコルを初期化して開く
     * @param config_path 設定ファイルのパス
     * @return エラーコード
     */
    virtual HakoPduErrorType open(const std::string& config_path) noexcept = 0;
    
    /**
     * @brief プロトコルを閉じる
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
     * @param endpoint 送信先エンドポイント
     * @param data 送信するデータへのポインタ
     * @param size 送信するデータのサイズ（バイト）
     * @return エラーコード
     */
    virtual HakoPduErrorType send(const Endpoint& endpoint, 
                                 const void* data, 
                                 size_t size) noexcept = 0;
    
    /**
     * @brief データを受信する
     * @param endpoint 受信元エンドポイント
     * @param data 受信バッファへのポインタ
     * @param buffer_size 受信バッファのサイズ（バイト）
     * @param[out] received_size 実際に受信したデータのサイズ（バイト）
     * @return エラーコード
     */
    virtual HakoPduErrorType recv(const Endpoint& endpoint, 
                                 void* data, 
                                 size_t buffer_size, 
                                 size_t& received_size) noexcept = 0;
};

}} // namespace hakoniwa::pdu
