#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSourcePollImpl::get_microseconds() const
{
    return 0;
}
void HakoniwaTimeSourcePollImpl::advance_time(uint64_t microseconds)
{
    (void)microseconds;
}
void HakoniwaTimeSourcePollImpl::sleep_delta_time() const
{
}

} // namespace hakoniwa::time_source
