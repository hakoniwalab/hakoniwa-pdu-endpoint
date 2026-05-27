#pragma once

#include <string>
#include <memory>
#include "hakoniwa/pdu/endpoint.hpp"

namespace benchmarks::runner {

class Runner {
public:
    virtual ~Runner() = default;
    virtual void prepare(std::string endpoint_config_path) = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

protected:
    std::unique_ptr<hakoniwa::pdu::Endpoint> endpoint_;
};

}