#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include <atomic>

namespace hakoniwa::time_source {

class HakoniwaTimeSource : public ITimeSource {
public:
    HakoniwaTimeSource() {}

    virtual uint64_t get_microseconds() const override;

    virtual void advance_time(uint64_t microseconds) override;
    virtual void sleep_delta_time() const override;
private:
    mutable std::atomic<uint64_t> current_time_microseconds_{0};

};

} // namespace hakoniwa::time_source
