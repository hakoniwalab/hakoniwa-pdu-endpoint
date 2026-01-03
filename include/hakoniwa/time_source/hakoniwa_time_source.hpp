#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include <atomic>

namespace hakoniwa::time_source {

class HakoniwaTimeSource : public ITimeSource {
public:
    HakoniwaTimeSource() : current_time_micros_(0) {}

    virtual uint64_t get_microseconds() const override {
        return current_time_micros_.load();
    }

    virtual void advance_time(uint64_t microseconds) override {
        current_time_micros_ += microseconds;
    }
    virtual void sleep_delta_time() const override {
        // No actual sleeping in Hakoniwa time source
    }

private:
    std::atomic<uint64_t> current_time_micros_;
};

} // namespace hakoniwa::pdu::bridge
