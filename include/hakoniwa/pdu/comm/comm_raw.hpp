#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"
#include "hakoniwa/pdu/comm/packet.hpp"
#include <vector>
#include <string>
#include <mutex> // Add mutex include
#include <memory>
// Removed <deque>, <mutex>, <condition_variable>

namespace hakoniwa {
namespace pdu {
namespace comm {

class PduCommRaw : public PduComm {
 public:
     PduCommRaw() = default;
     virtual ~PduCommRaw() = default;
 
     // PduComm interface implementation
     // These methods translate the PDU-level API to a raw byte-level API.
 
     HakoPduErrorType open(const std::string& config_path) override {
         return raw_open(config_path);
     }
 
     HakoPduErrorType close() noexcept override {
         return raw_close();
     }
 
     HakoPduErrorType start() noexcept override {
         return raw_start();
     }
 
     HakoPduErrorType stop() noexcept override {
         return raw_stop();
     }
 
     HakoPduErrorType is_running(bool& running) noexcept override {
         return raw_is_running(running);
     }
 
     HakoPduErrorType send(const PduResolvedKey& pdu_key, std::span<const std::byte> data) noexcept override {
         DataPacket packet(pdu_key.robot, pdu_key.channel_id, {data.begin(), data.end()});
         // TODO: Timestamps should be set here if needed.
         std::lock_guard<std::mutex> lock(send_mutex_); // Add lock
         auto encoded_data = packet.encode("v2"); // Encode data while holding lock
         return raw_send(encoded_data); // Call the pure virtual raw_send, now protected by the lock
     }
 
     HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override {
         // As per discussion, synchronous recv is handled by the Endpoint layer using the cache.
         // This PduCommRaw layer only supports asynchronous reception via callback.
         received_size = 0;
         return HAKO_PDU_ERR_UNSUPPORTED;
     }
 
 protected:
     // Pure virtual interface for derived classes (UdpComm, TcpComm)
     // These methods deal with raw, framed byte buffers.
 
     virtual HakoPduErrorType raw_open(const std::string& config_path) = 0;
     virtual HakoPduErrorType raw_close() noexcept = 0;
     virtual HakoPduErrorType raw_start() noexcept = 0;
     virtual HakoPduErrorType raw_stop() noexcept = 0;
     virtual HakoPduErrorType raw_is_running(bool& running) noexcept = 0;
     virtual HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept = 0; // Keep the original name
     
     // Method for derived classes to call when a raw packet is received
     void on_raw_data_received(const std::vector<std::byte>& raw_data) {
         auto packet = DataPacket::decode(raw_data, "v2");
         if (!packet) {
             // Decode error, maybe log it.
             return;
         }
 
         if (on_recv_callback_) {
             PduResolvedKey key;
             key.robot = packet->get_robot_name();
             key.channel_id = packet->get_channel_id();
             const auto& pdu_data = packet->get_pdu_data();
             on_recv_callback_(key, std::span<const std::byte>(pdu_data));
         }
         // Removed queue related code
     }
 
 private:
     std::mutex send_mutex_; // Add mutex member
 
     // Removed queue for synchronous recv
 };
 
 } // namespace comm
 } // namespace pdu
 } // namespace hakoniwa
