#pragma once

#include "hakoniwa/pdu/comm/comm.hpp"
#include <memory>
#include <string>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

class CommMultiplexer
{
public:
    virtual ~CommMultiplexer() = default;

    // Load mux comm configuration (e.g., TCP mux).
    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    // Close and release resources.
    virtual HakoPduErrorType close() noexcept = 0;
    // Start accepting/processing connections.
    virtual HakoPduErrorType start() noexcept = 0;
    // Stop accepting/processing connections.
    virtual HakoPduErrorType stop() noexcept = 0;

    // Non-blocking: returns newly created session comms; empty if none.
    virtual std::vector<std::shared_ptr<PduComm>> take_sessions() = 0;

    // Connection counters for readiness checks.
    virtual size_t connected_count() const noexcept = 0;
    virtual size_t expected_count() const noexcept = 0;

    bool is_ready() const noexcept { return connected_count() >= expected_count(); }
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
