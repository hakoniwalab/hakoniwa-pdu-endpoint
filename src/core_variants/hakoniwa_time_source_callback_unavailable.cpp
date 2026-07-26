#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"

namespace hakoniwa::time_source {

uint64_t HakoniwaTimeSourceCallbackImpl::get_microseconds() const
{
    return 0;
}

void HakoniwaTimeSourceCallbackImpl::advance_time(uint64_t)
{
}

} // namespace hakoniwa::time_source
