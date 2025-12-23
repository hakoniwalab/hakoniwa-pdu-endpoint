#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <string>

namespace hakoniwa {
namespace pdu
{

class Endpoint
{
public:
    Endpoint(const std::string& name, HakoPduEndpointDirectionType type) 
        : name_(name), type_(type) {}
    ~Endpoint() = default;
    
    Endpoint(const Endpoint&) = default;
    Endpoint(Endpoint&&) = default;
    Endpoint& operator=(const Endpoint&) = default;
    Endpoint& operator=(Endpoint&&) = default;
    
    const std::string& get_name() const { return name_; }
    HakoPduEndpointDirectionType get_type() const { return type_; }

private:
    std::string                  name_;
    HakoPduEndpointDirectionType type_;
};

}} // hakoniwa.pdu
