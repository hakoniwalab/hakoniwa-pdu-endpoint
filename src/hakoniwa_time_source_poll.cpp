#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"
#include "hakoniwa_asset_polling.h"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSourcePollImpl::get_microseconds() const
{
    hako_time_t t = hakoniwa_asset_get_worldtime();
    return static_cast<uint64_t>(t);
}
void HakoniwaTimeSourcePollImpl::advance_time(uint64_t microseconds)
{
    //How to advance time? Is there a use case for Hakoniwa Conductor? Probably not, I think.
    (void)microseconds;
}

} // namespace hakoniwa::time_source
