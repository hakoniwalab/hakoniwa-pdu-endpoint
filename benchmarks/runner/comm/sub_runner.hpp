#pragma once

#include "runner.hpp"

#include <atomic>

namespace benchmarks::runner {

class SubShmRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class SubTcpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class SubUdpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

}
