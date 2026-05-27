#pragma once

#include "runner.hpp"

namespace benchmarks::runner {

class SubShmRunner : public Runner {
public:
    void prepare(int num, std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

class SubTcpRunner : public Runner {
public:
    void prepare(int num, std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

class SubUdpRunner : public Runner {
public:
    void prepare(int num, std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

}
