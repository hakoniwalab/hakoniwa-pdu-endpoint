#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"
#include "hakoniwa/hako_capi.h"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSourceCallbackImpl::get_microseconds() const
{
    hako_time_t t = hako_asset_get_worldtime();
    return static_cast<uint64_t>(t);
}
void HakoniwaTimeSourceCallbackImpl::advance_time(uint64_t microseconds)
{
    (void)microseconds;
}
void HakoniwaTimeSourceCallbackImpl::sleep_delta_time() const
{
}

} // namespace hakoniwa::time_source
