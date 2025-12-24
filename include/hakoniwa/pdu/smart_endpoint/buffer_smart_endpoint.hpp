#pragma once

#include "hakoniwa/pdu/smart_endpoint.hpp" // SmartStoreEndpoint, PduFrameView
#include <deque>
#include <fstream> // Required for file input
#include <mutex>
#include <nlohmann/json.hpp> // Required for JSON parsing
#include <unordered_map>
#include <vector>

namespace hakoniwa {
namespace pdu {

class LatestOnlyBufferSmartEndpoint : public ISmartStoreEndpoint {
public:
  HakoPduErrorType store_frame(const PduFrameView& frame) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{frame.meta.robot, frame.meta.channel_id};

    auto& entry = buffers_[key];
    entry.data.assign(frame.body.begin(), frame.body.end());
    entry.has_data = true;

    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType write(const std::string& robot,
                         hako_pdu_uint32_t channel,
                         std::span<const std::byte> body) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{robot, channel};
    auto& entry = buffers_[key];
    entry.data.assign(body.begin(), body.end());
    entry.has_data = true;

    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType read(std::string& robot,
                        hako_pdu_uint32_t& channel,
                        std::span<std::byte> body_buf,
                        hako_pdu_uint32_t& body_len) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{robot, channel};
    auto it = buffers_.find(key);
    if (it == buffers_.end() || !it->second.has_data) {
      body_len = 0;
      return HAKO_PDU_ERR_NO_ENTRY;
    }

    const auto& src = it->second.data;
    if (body_buf.size() < src.size()) {
      body_len = static_cast<hako_pdu_uint32_t>(src.size());
      return HAKO_PDU_ERR_NO_SPACE;
    }

    std::copy(src.begin(), src.end(), body_buf.begin());
    body_len = static_cast<hako_pdu_uint32_t>(src.size());

    return HAKO_PDU_ERR_OK;
  }

private:
  struct BufferEntry {
    std::vector<std::byte> data;
    bool has_data{false};
  };

  std::mutex mtx_;
  std::unordered_map<PduKey, BufferEntry, PduKeyHash> buffers_;
};

class LatestQueueSmartEndpoint : public ISmartStoreEndpoint {
public:
  explicit LatestQueueSmartEndpoint(std::size_t depth)
    : depth_(depth) {}

  HakoPduErrorType store_frame(const PduFrameView& frame) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{frame.meta.robot, frame.meta.channel_id};
    auto& q = queues_[key].queue;

    q.emplace_back(frame.body.begin(), frame.body.end());

    if (q.size() > depth_) {
      q.pop_front(); // 古いのを捨てる
    }

    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType write(const std::string& robot,
                         hako_pdu_uint32_t channel,
                         std::span<const std::byte> body) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{robot, channel};
    auto& q = queues_[key].queue;

    q.emplace_back(body.begin(), body.end());
    if (q.size() > depth_) {
      q.pop_front();
    }

    return HAKO_PDU_ERR_OK;
  }

  HakoPduErrorType read(std::string& robot,
                        hako_pdu_uint32_t& channel,
                        std::span<std::byte> body_buf,
                        hako_pdu_uint32_t& body_len) noexcept override {
    std::lock_guard<std::mutex> lock(mtx_);

    PduKey key{robot, channel};
    auto it = queues_.find(key);
    if (it == queues_.end() || it->second.queue.empty()) {
      body_len = 0;
      return HAKO_PDU_ERR_NO_ENTRY;
    }

    auto& q = it->second.queue;
    const auto& src = q.front();

    if (body_buf.size() < src.size()) {
      body_len = static_cast<hako_pdu_uint32_t>(src.size());
      return HAKO_PDU_ERR_NO_SPACE;
    }

    std::copy(src.begin(), src.end(), body_buf.begin());
    body_len = static_cast<hako_pdu_uint32_t>(src.size());

    q.pop_front(); // ★ consume

    return HAKO_PDU_ERR_OK;
  }

private:
  struct QueueEntry {
    std::deque<std::vector<std::byte>> queue;
  };

  std::size_t depth_;
  std::mutex mtx_;
  std::unordered_map<PduKey, QueueEntry, PduKeyHash> queues_;
};


class BufferSmartEndpoint final : public SmartStoreEndpoint {
public:
  HakoPduErrorType open(const std::string& config_path) override {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
      return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    nlohmann::json json_config;
    try {
      ifs >> json_config;
    } catch (const nlohmann::json::parse_error& e) {
      // Consider logging the error: e.what()
      return HAKO_PDU_ERR_INVALID_JSON;
    }

    std::string mode;
    try {
      mode = json_config.at("store").at("mode").get<std::string>();
    } catch (const nlohmann::json::exception& e) {
      // Consider logging the error
      return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    if (mode == "latest") {
      impl_ = std::make_unique<LatestOnlyBufferSmartEndpoint>();
    } else if (mode == "queue") {
      int depth = 1; // Default depth as per schema
      try {
        depth = json_config.at("store").at("depth").get<int>();
      } catch (const nlohmann::json::exception& e) {
        // If depth is not provided for queue, use default.
        // Consider logging a warning if default is used.
      }
      impl_ = std::make_unique<LatestQueueSmartEndpoint>(depth);
    } else {
      return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    
    // Set the name of the endpoint from config
    try {
        name_ = json_config.at("name").get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        // Name is required, if not found, return error
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    return HAKO_PDU_ERR_OK;
  }
  
  HakoPduErrorType on_frame(const PduFrameView& frame) noexcept override {
    return store_frame(frame);
  }

  HakoPduErrorType close() noexcept override { impl_.reset(); return HAKO_PDU_ERR_OK; }
  HakoPduErrorType start() noexcept override { return HAKO_PDU_ERR_OK; }
  HakoPduErrorType stop() noexcept override { return HAKO_PDU_ERR_OK; }
  HakoPduErrorType is_running(bool& running) noexcept override { running = true; return HAKO_PDU_ERR_OK; }

  HakoPduErrorType store_frame(const PduFrameView& frame) noexcept override {
    return impl_ ? impl_->store_frame(frame) : HAKO_PDU_ERR_NO_ENTRY;
  }

  HakoPduErrorType write(const std::string& robot, hako_pdu_uint32_t ch,
                         std::span<const std::byte> body) noexcept override {
    return impl_ ? impl_->write(robot, ch, body) : HAKO_PDU_ERR_NO_ENTRY;
  }

  HakoPduErrorType read(std::string& robot, hako_pdu_uint32_t& ch,
                        std::span<std::byte> buf, hako_pdu_uint32_t& len) noexcept override {
    return impl_ ? impl_->read(robot, ch, buf, len) : HAKO_PDU_ERR_NO_ENTRY;
  }

private:
  std::unique_ptr<ISmartStoreEndpoint> impl_;
};

} // namespace pdu
} // namespace hakoniwa
