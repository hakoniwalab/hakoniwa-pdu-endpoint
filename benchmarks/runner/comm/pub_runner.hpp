#pragma once

#include "runner.hpp"

namespace benchmarks::runner {

class PubShmRunner : public Runner {
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
