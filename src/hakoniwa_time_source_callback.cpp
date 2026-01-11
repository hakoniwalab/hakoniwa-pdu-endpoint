#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"
#include "hakoniwa/hako_asset.h"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSourceCallbackImpl::get_microseconds() const
{
    hako_time_t t = hako_asset_simulation_time();
    return static_cast<uint64_t>(t);
}
void HakoniwaTimeSourceCallbackImpl::advance_time(uint64_t microseconds)
{
    (void)hako_asset_usleep(microseconds);
}

} // namespace hakoniwa::time_source
