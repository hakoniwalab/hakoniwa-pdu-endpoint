#pragma once

#include "endpoint_types.h"
#include "raw_endpoint.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hakoniwa::pdu {

// ========== Common types ==========

struct PduMeta {
  hako_pdu_uint32_t magic;
  hako_pdu_uint16_t ver;
  hako_pdu_uint16_t flags;
  hako_pdu_uint32_t request_type;
  hako_pdu_uint32_t body_len;
  hako_pdu_uint32_t total_len;

  std::string       robot;
  hako_pdu_uint32_t channel_id;

  hako_pdu_uint64_t hako_time_us;
  hako_pdu_uint64_t asset_time_us;
  hako_pdu_uint64_t real_time_us;
};

// Smartが扱う最小単位（decode済み）
struct PduFrameView {
  PduMeta meta;
  std::span<const std::byte> body;   // meta.body_len で切り出された payload
  std::span<const std::byte> raw;    // optional: 元フレーム（不要なら空でOK）
};

enum class DecisionType : uint8_t {
  Pass,
  Drop,
  Hold  // RateLimit等で「今は流さない」が必要な場合に使用
};

struct Decision {
  DecisionType type{DecisionType::Pass};
  const char*  reason{nullptr};  // 運用用。nullptrなら理由なし
};


// ========== SmartEndpoint (base layer) ==========
// 「意味を持つ」エンドポイントの共通基底。
// ただし read/write は持たない。
// Filter/RateLimit/Recorder/DebugDump/Buffer/SHM 全てがこれを継承できる。
class SmartEndpoint {
public:
  SmartEndpoint() = default;
  virtual ~SmartEndpoint() = default;

  // --- lifecycle ---
  virtual HakoPduErrorType open(const std::string& config_path) = 0;
  virtual HakoPduErrorType close() noexcept = 0;
  virtual HakoPduErrorType start() noexcept = 0;
  virtual HakoPduErrorType stop() noexcept = 0;
  virtual HakoPduErrorType is_running(bool& running) noexcept = 0;

  // --- naming / direction ---
  const std::string& get_name() const { return name_; }
  HakoPduEndpointDirectionType get_type() const { return type_; }

  // --- pipeline wiring (fan-out) ---
  // 下流に複数接続できる（fan-out）。
  // 例: Buffer + DebugDump を同時に接続
  void connect(std::shared_ptr<SmartEndpoint> next) {
    next_.push_back(std::move(next));
  }

  // --- input ---
  // Smart世界の入口：decode/正規化済みのフレームを受け取る
  virtual HakoPduErrorType on_frame(const PduFrameView& frame) noexcept = 0;

protected:
  // 下流へ流す（派生から使う）
  void forward(const PduFrameView& frame) noexcept {
    for (auto& n : next_) {
      (void)n->on_frame(frame);
    }
  }

  std::string                  name_;
  HakoPduEndpointDirectionType type_{HAKO_PDU_ENDPOINT_DIRECTION_INOUT};

private:
  std::vector<std::shared_ptr<SmartEndpoint>> next_;
};


// ========== SmartEndpointBase (optional helper) ==========
// evaluate + forward の典型パターンを共通化。
// Filter/RateLimit/Recorder/DebugDump などはこれを継承すると実装が爆速になる。
class SmartEndpointBase : public SmartEndpoint {
public:
  HakoPduErrorType on_frame(const PduFrameView& frame) noexcept override {
    Decision d = evaluate(frame);
    switch (d.type) {
      case DecisionType::Pass:
        on_pass(frame, d);
        forward(frame);
        break;
      case DecisionType::Hold:
        on_hold(frame, d);
        break;
      case DecisionType::Drop:
        on_drop(frame, d);
        break;
    }
    return HAKO_PDU_ERR_OK;
  }

protected:
  virtual Decision evaluate(const PduFrameView& frame) noexcept = 0;

  // 必要なら override
  virtual void on_pass(const PduFrameView&, const Decision&) noexcept {}
  virtual void on_hold(const PduFrameView&, const Decision&) noexcept {}
  virtual void on_drop(const PduFrameView&, const Decision&) noexcept {}
};


// ========== SmartStoreEndpoint (2nd layer) ==========
// 「状態を保持して read/write で提供する」Smart。
// BufferSmartEndpoint / ShmSmartEndpoint などはこれを実装する。
class SmartStoreEndpoint : public SmartEndpoint {
public:
  SmartStoreEndpoint() = default;
  ~SmartStoreEndpoint() override = default;

  // bodyは「PDU payload 部分」（meta/body_len で示される部分）
  virtual HakoPduErrorType write(const std::string& robot_name,
                                 hako_pdu_uint32_t channel_id,
                                 std::span<const std::byte> body) noexcept = 0;

  // 呼び出し側が buffer を用意し、そこへコピーする方式
  // body_len は in/out：入力は最大、出力は実サイズ
  virtual HakoPduErrorType read(std::string& robot_name,
                                hako_pdu_uint32_t& channel_id,
                                std::span<std::byte> body_buf,
                                hako_pdu_uint32_t& body_len) noexcept = 0;
};



// ========== CommSmartEndpoint (bridge raw <-> smart) ==========
// Endpointから受け取った bytes を decode/正規化して SmartEndpoint へ流す。
// また Smart側から送信する場合は encode して Rawへ渡す。
// ※「PDUメタデータ解析」はこの層の責務にするのが最も安全。
class SmartCommEndpoint {
public:
  SmartCommEndpoint() = default;

  void attach_raw(std::shared_ptr<RawEndpoint> raw) { raw_ = std::move(raw); }
  void attach_smart_root(std::shared_ptr<SmartEndpoint> root) { smart_root_ = std::move(root); }

  // raw->recv() を回して decode し、smart_root_->on_frame() に渡す
  HakoPduErrorType poll_recv() noexcept {
    if (!raw_ || !smart_root_) return HAKO_PDU_ERR_INVALID_ARGUMENT;

    std::vector<std::byte> bytes;
    size_t received_size = 0;
    auto err = raw_->recv(bytes, received_size);
    if (err != HAKO_PDU_ERR_OK) return err;
    if (bytes.empty()) return HAKO_PDU_ERR_OK;

    PduFrameView frame{};
    err = decode(bytes, frame);
    if (err != HAKO_PDU_ERR_OK) return err;

    return smart_root_->on_frame(frame);
  }

  // Smart側から送る：meta+body を encode して raw_->send()
  HakoPduErrorType send_frame(const PduMeta& meta, std::span<const std::byte> body) noexcept {
    if (!raw_) return HAKO_PDU_ERR_INVALID_ARGUMENT;
    std::vector<std::byte> bytes;
    auto err = encode(meta, body, bytes);
    if (err != HAKO_PDU_ERR_OK) return err;
    return raw_->send(bytes);
  }

  // 実装に応じて v1/v2 をここで吸収する
  // decodeは「body_len検証」「magic/version検証」を必ずやるのが吉
  virtual HakoPduErrorType decode(const std::vector<std::byte>& raw_bytes,
                                  PduFrameView& out_frame) noexcept = 0;

  virtual HakoPduErrorType encode(const PduMeta& meta,
                                  std::span<const std::byte> body,
                                  std::vector<std::byte>& out_bytes) noexcept = 0;

private:
  std::shared_ptr<RawEndpoint>    raw_;
  std::shared_ptr<SmartEndpoint> smart_root_;
};

} // namespace hakoniwa::pdu
