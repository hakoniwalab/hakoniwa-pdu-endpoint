#pragma once

#include <chrono>
#include <cstdint> // For uint64_t

namespace hakoniwa::time_source {

class ITimeSource {
public:
    virtual ~ITimeSource() = default;

    void set_delta_time_microseconds(uint64_t delta_us)
    {
        delta_time_microseconds_ = delta_us;
    }
    uint64_t get_delta_time_microseconds() const
    {
        return delta_time_microseconds_;
    }

    // Returns the current time in microseconds
    virtual uint64_t get_microseconds() const = 0;
    virtual void sleep_delta_time() const = 0;

    // Optional: for virtual time, to advance time (may be implemented in derived classes)
    virtual void advance_time(uint64_t microseconds) { (void)microseconds; }
protected:
    uint64_t delta_time_microseconds_ = 0;
};

} // namespace hakoniwa::pdu::bridge
