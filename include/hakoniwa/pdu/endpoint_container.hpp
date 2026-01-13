#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include "hakoniwa/pdu/endpoint.hpp"  // your Endpoint

namespace hakoniwa::pdu {

struct EndpointEntry {
  std::string id;
  std::string config_path;
  std::optional<HakoPduEndpointDirectionType> direction;
  std::optional<std::string> mode;
};

class EndpointContainer {
public:
  EndpointContainer(std::string node_id, std::string container_config_path);

  HakoPduErrorType create_pdu_lchannels();

  // Parse config only
  HakoPduErrorType initialize();

  // Lifecycle owner
  HakoPduErrorType start_all() noexcept;
  HakoPduErrorType stop_all() noexcept;
  bool is_running_all() const noexcept;

  // Optional: explicit per-endpoint lifecycle
  HakoPduErrorType start(const std::string& endpoint_id) noexcept;
  HakoPduErrorType stop(const std::string& endpoint_id) noexcept;
  std::shared_ptr<Endpoint> ref(const std::string& id);

  std::vector<std::string> list_endpoint_ids() const;
  const std::string& node_id() const { return node_id_; }
  const std::string& last_error() const { return last_error_; }

private:
  std::optional<EndpointEntry> find_entry_(const std::string& endpoint_id) const;

  // create & open endpoint (called under lock)
  std::shared_ptr<Endpoint> create_and_open_(const EndpointEntry& e);
  std::shared_ptr<Endpoint> create_(const EndpointEntry& e);

  std::string node_id_;
  std::string container_config_path_;
  std::vector<EndpointEntry> entries_;

  mutable std::mutex mtx_;
  std::unordered_map<std::string, std::shared_ptr<Endpoint>> cache_; // id -> endpoint

  // track started state (optional but practical)
  std::unordered_map<std::string, bool> started_;
  bool initialized_{false};

  std::string last_error_;
};

} // namespace hakoniwa::pdu
