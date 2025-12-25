#pragma once

#include "hakoniwa/pdu/cache/cache.hpp"
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
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
  bool is_running_ = false;

public:
  PduLatestBuffer() = default;
  ~PduLatestBuffer() override = default;
  PduLatestBuffer(const PduLatestBuffer &) = delete;
  PduLatestBuffer(PduLatestBuffer &&) = delete;
  PduLatestBuffer &operator=(const PduLatestBuffer &) = delete;
  PduLatestBuffer &operator=(PduLatestBuffer &&) = delete;

  HakoPduErrorType open(const std::string &config_path) override {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
      return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    nlohmann::json json_config;
    try {
      ifs >> json_config;
      if (!json_config.contains("type") || json_config["type"] != "buffer") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
      }
      if (!json_config.contains("store") || !json_config["store"].contains("mode") || json_config["store"]["mode"] != "latest") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
      }
    } catch (const nlohmann::json::exception&) {
      return HAKO_PDU_ERR_INVALID_JSON;
    }
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