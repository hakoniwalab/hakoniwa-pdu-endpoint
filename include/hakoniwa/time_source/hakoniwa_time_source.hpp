#pragma once

#include "hakoniwa/time_source/time_source.hpp"
#include "hakoniwa/time_source/hakoniwa_time_source_impl.hpp"
#include <memory>

namespace hakoniwa::time_source {

class HakoniwaTimeSource : public ITimeSource {
public:
    enum class ImplType {
        Poll,
        Callback
    };

    HakoniwaTimeSource();
    explicit HakoniwaTimeSource(ImplType impl_type);
    explicit HakoniwaTimeSource(std::unique_ptr<IHakoniwaTimeSourceImpl> impl);

    virtual uint64_t get_microseconds() const override;

    virtual void advance_time(uint64_t microseconds) override;
    virtual void sleep_delta_time() const override;
private:
    std::unique_ptr<IHakoniwaTimeSourceImpl> impl_;

};

} // namespace hakoniwa::time_source
