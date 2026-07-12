#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include "hakoniwa/time_source/real_time_source.hpp"
#include "hakoniwa/time_source/virtual_time_source.hpp"
#ifndef HAKO_PDU_ENDPOINT_DISABLE_HAKONIWA_CORE
#include "hakoniwa/time_source/hakoniwa_time_source.hpp"
#endif

#include <stdexcept>
#include <string>

namespace hakoniwa::time_source {
static inline std::unique_ptr<ITimeSource> create_time_source(
    const std::string& type,
    uint64_t delta_time_step_usec)
{
    std::unique_ptr<ITimeSource> time_source;

    if (type == "real") {
        time_source = std::make_unique<RealTimeSource>();
    } else if (type == "virtual") {
        time_source = std::make_unique<VirtualTimeSource>();
#ifndef HAKO_PDU_ENDPOINT_DISABLE_HAKONIWA_CORE
    } else if (type == "hakoniwa") {
        time_source = std::make_unique<HakoniwaTimeSource>(HakoniwaTimeSource::ImplType::Poll);
    } else if (type == "hakoniwa_poll") {
        time_source = std::make_unique<HakoniwaTimeSource>(HakoniwaTimeSource::ImplType::Poll);
    } else if (type == "hakoniwa_callback") {
        time_source = std::make_unique<HakoniwaTimeSource>(HakoniwaTimeSource::ImplType::Callback);
#else
    } else if (type == "hakoniwa" || type == "hakoniwa_poll" || type == "hakoniwa_callback") {
        throw std::invalid_argument("Hakoniwa core time source is disabled: " + type);
#endif
    } else {
        throw std::invalid_argument("Unknown time source type: " + type);
    }

    time_source->set_delta_time_microseconds(delta_time_step_usec);
    return time_source;
}
} // namespace hakoniwa::time_source
