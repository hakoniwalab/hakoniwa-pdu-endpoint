#pragma once

#include "comm/runner.hpp"

namespace benchmarks::runner {

class PubShmRunner : public Runner {
public:
    void prepare(std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

class PubTcpRunner : public Runner {
public:
    void prepare(std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

class PubUdpRunner : public Runner {
public:
    void prepare(std::string endpoint_config_path) override;
    void run() override;
    void cleanup() override;
};

}
