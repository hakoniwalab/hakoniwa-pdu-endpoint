#pragma once

#include <string>
#include "hakoniwa/pdu/endpoint.hpp"

namespace benchmarks::runner {

class Runner {
public:
    virtual ~Runner() = default;
    virtual void prepare(std::string endpoint) = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

protected:
    hakoniwa::pdu::Endpoint* endpoint_ = nullptr;
};

}