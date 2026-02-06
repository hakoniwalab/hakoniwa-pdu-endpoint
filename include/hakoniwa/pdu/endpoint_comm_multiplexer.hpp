#pragma once

#include "hakoniwa/pdu/endpoint_types.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include "hakoniwa/pdu/comm/comm_mux.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hakoniwa {
namespace pdu {

class EndpointCommMultiplexer
{
public:
    EndpointCommMultiplexer(const std::string& name, HakoPduEndpointDirectionType type);

    // Notes:
    // - This class is protocol-agnostic; the comm multiplexer is selected by the comm config.
    // - take_endpoints() is non-blocking. If no new connections are ready, it returns an empty vector.
    // - Returned endpoints are already opened and started; the caller can use them immediately.
    // - Endpoint names are generated as "<mux_name>_<seq>" (seq starts at 1).
    // - expected/connected counts are managed by the comm multiplexer (e.g., TCP mux).
    // Load mux endpoint config and initialize comm multiplexer.
    HakoPduErrorType open(const std::string& endpoint_mux_config_path);
    HakoPduErrorType close() noexcept;
    HakoPduErrorType start() noexcept;
    HakoPduErrorType stop() noexcept;

    // Non-blocking: returns any newly accepted endpoints; empty if none.
    std::vector<std::unique_ptr<Endpoint>> take_endpoints();

    // Connection counters are driven by comm multiplexer (e.g., TCP mux).
    size_t connected_count() const noexcept;
    size_t expected_count() const noexcept;
    bool is_ready() const noexcept;

private:
    std::unique_ptr<comm::CommMultiplexer> create_comm_mux_(const std::string& comm_config_path);

    std::string name_;
    HakoPduEndpointDirectionType type_;
    std::string endpoint_config_path_;
    std::filesystem::path base_dir_;
    std::unique_ptr<comm::CommMultiplexer> comm_;
    size_t endpoint_seq_ = 0;
};

} // namespace pdu
} // namespace hakoniwa
