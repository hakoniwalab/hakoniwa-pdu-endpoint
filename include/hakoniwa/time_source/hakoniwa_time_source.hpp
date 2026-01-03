#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include "hako_asset.h"
#include <atomic>

namespace hakoniwa::time_source {

class HakoniwaTimeSource : public ITimeSource {
public:
    HakoniwaTimeSource() {}

    virtual uint64_t get_microseconds() const override {
        hako_time_t t = hako_asset_simulation_time();
        return static_cast<uint64_t>(t);
    }

    virtual void advance_time(uint64_t microseconds) override {
        //noop
    }
    virtual void sleep_delta_time() const override {
        hako_time_t sleep_time_usec = static_cast<hako_time_t>(delta_time_microseconds_);
        hako_asset_usleep(sleep_time_usec);
    }

};

} // namespace hakoniwa::time_source
