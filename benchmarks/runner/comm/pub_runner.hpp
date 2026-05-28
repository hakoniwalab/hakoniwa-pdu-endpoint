#pragma once

#include "runner.hpp"
#include "shm_runner.hpp"

namespace benchmarks::runner {

class PubShmRunner : public ShmRunnerBase {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class PubTcpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class PubUdpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

}
