#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hakoniwa {
namespace pdu {
class PduCache
{
public:
    virtual ~PduCache() = default;
    // コピー・ムーブ禁止（ポリモーフィックな基底クラス）
    PduCache(const PduCache&) = delete;
    PduCache(PduCache&&) = delete;
    PduCache& operator=(const PduCache&) = delete;
    PduCache& operator=(PduCache&&) = delete;

    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    virtual HakoPduErrorType close() noexcept = 0;
    virtual HakoPduErrorType start() noexcept = 0;
    virtual HakoPduErrorType stop() noexcept = 0;
    virtual HakoPduErrorType is_running(bool& running) noexcept = 0;

    virtual HakoPduErrorType write(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept = 0;
    virtual HakoPduErrorType read(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept = 0;
    

};
} // namespace pdu
} // namespace hakoniwa
