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

    virtual HakoPduErrorType open(const std::string& config_path) = 0;
    virtual HakoPduErrorType close() noexcept = 0;
    virtual HakoPduErrorType start() noexcept = 0;
    virtual HakoPduErrorType stop() noexcept = 0;

    virtual std::vector<std::shared_ptr<PduComm>> take_sessions() = 0;

    virtual size_t connected_count() const noexcept = 0;
    virtual size_t expected_count() const noexcept = 0;

    bool is_ready() const noexcept { return connected_count() >= expected_count(); }
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
