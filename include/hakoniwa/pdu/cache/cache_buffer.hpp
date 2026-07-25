#pragma once

#include "hakoniwa/pdu/cache/cache.hpp"
#include "hakoniwa/pdu/cache/cache_config_json.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <deque>
#include <unordered_map>
#include <vector>
#include <iostream>

namespace hakoniwa {
namespace pdu {

class PduLatestBuffer : public PduCache {
private:
  struct BufferEntry {
    std::vector<std::byte> data;
    bool has_data = false;
  };

  std::mutex mtx_;
  std::unordered_map<PduResolvedKey, BufferEntry, PduResolvedKeyHash> buffers_;
  std::deque<PduResolvedKey> pending_order_;
  std::unordered_map<PduResolvedKey, bool, PduResolvedKeyHash> pending_keys_;
  std::unordered_map<PduResolvedKey, bool, PduResolvedKeyHash> tracked_keys_;
  bool is_running_ = false;

public:
  PduLatestBuffer() = default;
  ~PduLatestBuffer() override = default;
  PduLatestBuffer(const PduLatestBuffer &) = delete;
  PduLatestBuffer(PduLatestBuffer &&) = delete;
  PduLatestBuffer &operator=(const PduLatestBuffer &) = delete;
  PduLatestBuffer &operator=(PduLatestBuffer &&) = delete;

  HakoPduErrorType open(const std::string &config_path) override {
    CacheConfig config;
    const auto result = load_cache_config(config_path, config);
    if (result != HAKO_PDU_ERR_OK) return result;
    return configure(config);
  }

  HakoPduErrorType configure(const CacheConfig& config) noexcept override {
    if (validate_cache_config(config) != HAKO_PDU_ERR_OK || config.mode != CacheMode::Latest) return HAKO_PDU_ERR_INVALID_CONFIG;
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType close() noexcept override { std::lock_guard<std::mutex> lock(mtx_); buffers_.clear(); pending_order_.clear(); pending_keys_.clear(); tracked_keys_.clear(); is_running_ = false; return HAKO_PDU_ERR_OK; }
  HakoPduErrorType start() noexcept override { is_running_ = true; return HAKO_PDU_ERR_OK; }
  HakoPduErrorType stop() noexcept override { is_running_ = false; return HAKO_PDU_ERR_OK; }
  HakoPduErrorType is_running(bool &running) noexcept override { running = is_running_; return HAKO_PDU_ERR_OK; }

  HakoPduErrorType write(const PduResolvedKey &pdu_key, std::span<const std::byte> data) noexcept override {
    if (!is_running_) return HAKO_PDU_ERR_NOT_RUNNING;
    std::lock_guard<std::mutex> lock(mtx_); auto &entry = buffers_[pdu_key]; entry.data.assign(data.begin(), data.end()); entry.has_data = true;
    if (!pending_keys_[pdu_key]) { pending_order_.push_back(pdu_key); pending_keys_[pdu_key] = true; }
    return HAKO_PDU_ERR_OK;
  }
  HakoPduErrorType read(const PduResolvedKey &pdu_key, std::span<std::byte> data, size_t &received_size) noexcept override {
    if (!is_running_) return HAKO_PDU_ERR_NOT_RUNNING;
    std::lock_guard<std::mutex> lock(mtx_); auto it = buffers_.find(pdu_key);
    if (it == buffers_.end() || !it->second.has_data) { received_size = 0; return HAKO_PDU_ERR_NO_ENTRY; }
    const auto &src = it->second.data; if (data.size() < src.size()) { received_size = src.size(); return HAKO_PDU_ERR_NO_SPACE; }
    std::copy(src.begin(), src.end(), data.begin()); received_size = src.size(); pending_keys_[pdu_key] = false; return HAKO_PDU_ERR_OK;
  }
  HakoPduErrorType set_recv_event(const PduResolvedKey &pdu_key) noexcept override { std::lock_guard<std::mutex> lock(mtx_); tracked_keys_[pdu_key] = true; return HAKO_PDU_ERR_OK; }
  HakoPduErrorType get_pending_count(size_t &out_count) noexcept override { std::lock_guard<std::mutex> lock(mtx_); out_count = 0; for (const auto &[key, tracked] : tracked_keys_) if (tracked) { auto it = pending_keys_.find(key); if (it != pending_keys_.end() && it->second) ++out_count; } return HAKO_PDU_ERR_OK; }
  HakoPduErrorType read_next(PduRecord &out) noexcept override {
    if (!is_running_) return HAKO_PDU_ERR_NOT_RUNNING;
    std::lock_guard<std::mutex> lock(mtx_);
    while (!pending_order_.empty()) { const auto key = pending_order_.front(); pending_order_.pop_front(); auto pending_it = pending_keys_.find(key); if (pending_it == pending_keys_.end() || !pending_it->second) continue; auto buffer_it = buffers_.find(key); if (buffer_it == buffers_.end() || !buffer_it->second.has_data) { pending_it->second = false; continue; } out.key = key; out.timestamp_ns = 0; out.payload = buffer_it->second.data; pending_it->second = false; return HAKO_PDU_ERR_OK; }
    return HAKO_PDU_ERR_NO_ENTRY;
  }
};

} // namespace pdu
} // namespace hakoniwa
