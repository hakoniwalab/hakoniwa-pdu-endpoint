#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief エンドポイントの方向タイプ
 */
typedef enum 
{
    HAKO_PDU_ENDPOINT_DIRECTION_IN = 0,    /**< 入力方向 */
    HAKO_PDU_ENDPOINT_DIRECTION_OUT = 1,   /**< 出力方向 */
    HAKO_PDU_ENDPOINT_DIRECTION_INOUT = 2  /**< 双方向 */
} HakoPduEndpointDirectionType;

/**
 * @brief エラーコード
 */
typedef enum
{
    HAKO_PDU_ERR_OK = 0,                /**< 成功 */
    HAKO_PDU_ERR_INVALID_ARGUMENT = 1,  /**< 無効な引数 */
    HAKO_PDU_ERR_OUT_OF_MEMORY = 2,     /**< メモリ不足 */
    HAKO_PDU_ERR_IO_ERROR = 3,          /**< I/Oエラー */
    HAKO_PDU_ERR_NO_SPACE = 4,          /**< 空き容量不足 */
    HAKO_PDU_ERR_BUSY = 5,              /**< リソース使用中 */
    HAKO_PDU_ERR_TIMEOUT = 6,           /**< タイムアウト */
    HAKO_PDU_ERR_NO_ENTRY = 7,          /**< エントリなし */
    HAKO_PDU_ERR_FILE_NOT_FOUND = 8,    /**< ファイルが見つからない */
    HAKO_PDU_ERR_INVALID_JSON = 9,      /**< JSONフォーマットが不正 */
    HAKO_PDU_ERR_INVALID_CONFIG = 10,   /**< 設定が不正 */
    HAKO_PDU_ERR_NOT_RUNNING = 11,      /**< エンドポイントが起動していない */
    HAKO_PDU_ERR_UNSUPPORTED = 12,      /**< サポートされていない操作 */
    HAKO_PDU_ERR_INVALID_PDU_KEY = 13   /**< PDUキーが不正 */
} HakoPduErrorType;

/**
 * @brief プラットフォーム独立な基本型定義
 * C言語用またはバイナリ互換性が必要な場合に使用
 */
typedef uint8_t  hako_pdu_bool_t;
typedef int8_t   hako_pdu_sint8_t;
typedef uint8_t  hako_pdu_uint8_t;
typedef int16_t  hako_pdu_sint16_t;
typedef uint16_t hako_pdu_uint16_t;
typedef int32_t  hako_pdu_sint32_t;
typedef uint32_t hako_pdu_uint32_t;
typedef int64_t  hako_pdu_sint64_t;
typedef uint64_t hako_pdu_uint64_t;
typedef float    hako_pdu_float32_t;
typedef double   hako_pdu_float64_t;

#define HAKO_PDU_TRUE  ((hako_pdu_bool_t)1)
#define HAKO_PDU_FALSE ((hako_pdu_bool_t)0)

#ifdef __cplusplus
}
#endif