#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include "hakoniwa/time_source/real_time_source.hpp"
#include "hakoniwa/time_source/virtual_time_source.hpp"
#include "hakoniwa/time_source/hakoniwa_time_source.hpp"

namespace hakoniwa::time_source {
std::unique_ptr<ITimeSource> create_time_source(const std::string& type, uint64_t delta_time_step_usec) 
{
    std::unique_ptr<ITimeSource> time_source;
    if (type == "real") {
        time_source = std::make_unique<RealTimeSource>();
    } else if (type == "virtual") {
        time_source = std::make_unique<VirtualTimeSource>();
    } else if (type == "hakoniwa") {
        time_source = std::make_unique<HakoniwaTimeSource>();
    } else {
        throw std::invalid_argument("Unknown time source type: " + type);
    }
    time_source->set_delta_time_microseconds(delta_time_step_usec);
    return time_source;
}
} // namespace hakoniwa::time_source