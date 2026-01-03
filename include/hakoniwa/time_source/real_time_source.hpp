#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include <chrono>
#include <thread>

namespace hakoniwa::time_source {

class RealTimeSource : public ITimeSource {
public:
    RealTimeSource()
      : start_us_(now_us()) {}


    uint64_t get_microseconds() const override {
        return now_us() - start_us_; // “起点からの経過us”
    }
    void sleep_delta_time() const override {
        std::this_thread::sleep_for(std::chrono::microseconds(delta_time_microseconds_));
    }

private:
    static uint64_t now_us() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    uint64_t start_us_;
};


} // namespace hakoniwa::pdu::bridge
