#pragma once

#include <cstdint>
#include <memory>

namespace hakoniwa::time_source {

class IHakoniwaTimeSourceImpl {
public:
    virtual ~IHakoniwaTimeSourceImpl() = default;

    virtual uint64_t get_microseconds() const = 0;
    virtual void advance_time(uint64_t microseconds) = 0;
};

class HakoniwaTimeSourcePollImpl : public IHakoniwaTimeSourceImpl {
public:
    HakoniwaTimeSourcePollImpl() = default;
    ~HakoniwaTimeSourcePollImpl() override = default;

    uint64_t get_microseconds() const override;
    void advance_time(uint64_t microseconds) override;
};

class HakoniwaTimeSourceCallbackImpl : public IHakoniwaTimeSourceImpl {
public:
    HakoniwaTimeSourceCallbackImpl() = default;
    ~HakoniwaTimeSourceCallbackImpl() override = default;

    uint64_t get_microseconds() const override;
    void advance_time(uint64_t microseconds) override;
};

} // namespace hakoniwa::time_source
