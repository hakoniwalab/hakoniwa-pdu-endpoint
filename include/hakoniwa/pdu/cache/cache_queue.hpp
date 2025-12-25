#pragma once

#include "hakoniwa/pdu/cache/cache.hpp"
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

namespace hakoniwa {
namespace pdu {

class PduLatestQueue : public PduCache {
private:
  struct QueueEntry {
    std::deque<std::vector<std::byte>> queue;
  };

  std::size_t depth_ = 1;
  std::mutex mtx_;
  std::unordered_map<PduResolvedKey, QueueEntry, PduResolvedKeyHash> queues_;
  bool is_running_ = false;

public:
  PduLatestQueue() = default;
  ~PduLatestQueue() override = default;
  PduLatestQueue(const PduLatestQueue &) = delete;
  PduLatestQueue(PduLatestQueue &&) = delete;
  PduLatestQueue &operator=(const PduLatestQueue &) = delete;
  PduLatestQueue &operator=(PduLatestQueue &&) = delete;

  HakoPduErrorType open(const std::string &config_path) override {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
      return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }
    nlohmann::json json_config;
    try {
      ifs >> json_config;
      if (json_config.contains("store") && json_config["store"].contains("depth")) {
        depth_ = json_config["store"]["depth"].get<int>();
      }
    } catch (const nlohmann::json::parse_error &e) {
      return HAKO_PDU_ERR_INVALID_JSON;
    } catch (const nlohmann::json::exception &e) {
      return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (depth_ == 0) {
        depth_ = 1; // depth must be >= 1
    }
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType close() noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);
    queues_.clear();
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
    auto &q = queues_[pdu_key].queue;
    q.emplace_back(data.begin(), data.end());

    if (q.size() > depth_) {
      q.pop_front();
    }
    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType read(const PduResolvedKey &pdu_key,
                        std::span<std::byte> data,
                        size_t &received_size) noexcept override {
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = queues_.find(pdu_key);
    if (it == queues_.end() || it->second.queue.empty()) {
      received_size = 0;
      return HAKO_PDU_ERR_NO_ENTRY;
    }

    auto &q = it->second.queue;
    const auto &src = q.front(); 

    if (data.size() < src.size()) {
      received_size = src.size();
      return HAKO_PDU_ERR_NO_SPACE;
    }

    std::copy(src.begin(), src.end(), data.begin());
    received_size = src.size();

    q.pop_front();

    return HAKO_PDU_ERR_OK;
  }
};

} // namespace pdu
} // namespace hakoniwa
