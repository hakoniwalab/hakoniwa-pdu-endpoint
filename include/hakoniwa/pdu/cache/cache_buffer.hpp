#pragma once

#include "hakoniwa/pdu/cache/cache.hpp"
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hakoniwa {
namespace pdu {

// PduResolvedKey のためのハッシュ関数と等価比較
// note: cache_queue.hppにも同じ定義があるため、将来的には共通ヘッダに移動することが望ましい
struct PduResolvedKeyHash {
  std::size_t operator()(const PduResolvedKey &k) const {
    return std::hash<std::string>()(k.robot) ^
           (std::hash<hako_pdu_uint32_t>()(k.channel_id) << 1);
  }
};

inline bool operator==(const PduResolvedKey &a, const PduResolvedKey &b) {
  return a.robot == b.robot && a.channel_id == b.channel_id;
}

class PduLatestBuffer : public PduCache {
private:
  struct BufferEntry {
    std::vector<std::byte> data;
    bool has_data = false;
  };

  std::mutex mtx_;
  std::unordered_map<PduResolvedKey, BufferEntry, PduResolvedKeyHash> buffers_;
  bool is_running_ = false;

public:
  PduLatestBuffer() = default;
  ~PduLatestBuffer() override = default;
  PduLatestBuffer(const PduLatestBuffer &) = delete;
  PduLatestBuffer(PduLatestBuffer &&) = delete;
  PduLatestBuffer &operator=(const PduLatestBuffer &) = delete;
  PduLatestBuffer &operator=(PduLatestBuffer &&) = delete;

  HakoPduErrorType open(const std::string &config_path) override {
    // この実装では設定ファイルは不要
    (void)config_path; //未使用引数警告を抑制
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType close() noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);
    buffers_.clear();
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType start() noexcept override {
    is_running_ = true;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType stop() noexcept override {
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType is_running(bool &running) noexcept override {
    running = is_running_;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType write(const PduResolvedKey &pdu_key,
                         std::span<const std::byte> data) noexcept override {
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    auto &entry = buffers_[pdu_key];
    entry.data.assign(data.begin(), data.end());
    entry.has_data = true;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType read(const PduResolvedKey &pdu_key,
                        std::span<std::byte> data,
                        size_t &received_size) noexcept override {
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = buffers_.find(pdu_key);
    if (it == buffers_.end() || !it->second.has_data) {
      received_size = 0;
      return HAKO_PDU_ERR_NO_ENTRY;
    }

    const auto &src = it->second.data;
    if (data.size() < src.size()) {
      received_size = src.size();
      return HAKO_PDU_ERR_NO_SPACE;
    }

    std::copy(src.begin(), src.end(), data.begin());
    received_size = src.size();
    // データは消費しない

    return HAKO_PDU_ERR_OK;
  }
};

} // namespace pdu
} // namespace hakoniwa
