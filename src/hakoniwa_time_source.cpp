#include "hakoniwa/time_source/hakoniwa_time_source.hpp"
#include "hakoniwa/hako_capi.h"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSource::get_microseconds() const 
{
    hako_time_t t = hako_asset_get_worldtime();
    return static_cast<uint64_t>(t);
}
void HakoniwaTimeSource::advance_time(uint64_t microseconds) 
{
    //noop
}
void HakoniwaTimeSource::sleep_delta_time() const {
    //noop
}
} // namespace hakoniwa::time_source