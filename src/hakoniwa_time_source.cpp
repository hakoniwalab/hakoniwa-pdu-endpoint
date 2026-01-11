#include "hakoniwa/time_source/hakoniwa_time_source.hpp"

namespace hakoniwa::time_source {

HakoniwaTimeSource::HakoniwaTimeSource()
    : impl_(std::make_unique<HakoniwaTimeSourceCallbackImpl>())
{
}

HakoniwaTimeSource::HakoniwaTimeSource(ImplType impl_type)
{
    switch (impl_type) {
    case ImplType::Poll:
        impl_ = std::make_unique<HakoniwaTimeSourcePollImpl>();
        break;
    case ImplType::Callback:
    default:
        impl_ = std::make_unique<HakoniwaTimeSourceCallbackImpl>();
        break;
    }
}

HakoniwaTimeSource::HakoniwaTimeSource(std::unique_ptr<IHakoniwaTimeSourceImpl> impl)
    : impl_(std::move(impl))
{
}

uint64_t HakoniwaTimeSource::get_microseconds() const 
{
    if (!impl_) {
        return 0;
    }
    return impl_->get_microseconds();
}
void HakoniwaTimeSource::advance_time(uint64_t microseconds) 
{
    if (!impl_) {
        return;
    }
    impl_->advance_time(microseconds);
}
void HakoniwaTimeSource::sleep_delta_time() const {
    if (!impl_) {
        return;
    }
    impl_->advance_time(delta_time_microseconds_);
}
} // namespace hakoniwa::time_source
