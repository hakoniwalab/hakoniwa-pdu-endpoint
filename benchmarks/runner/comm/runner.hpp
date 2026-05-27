#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include "hakoniwa/pdu/endpoint.hpp"
#include "geometry_msgs/pdu_cpptype_conv_Twist.hpp"

namespace benchmarks::runner {

class Runner {
public:
    virtual ~Runner() = default;
    virtual void prepare(int num, std::string endpoint_config_path) = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

protected:
    void create_send_buffer_for_key(int num)
    {
        pdu_keys_.clear();
        pdu_sizes_.clear();
        buf_.clear();
        send_size_ = 0;

        for (int i = 0; i < num; ++i) {
            hakoniwa::pdu::PduKey key = {"Drone-" + std::to_string(i + 1), "pos"};

            size_t pdu_size = endpoint_->get_pdu_size(key);
            if (pdu_size == 0) {
                throw std::runtime_error(
                    "Failed to get PDU size for key: " + key.robot + "/" + key.pdu);
            }

            pdu_keys_.push_back(key);
            pdu_sizes_.push_back(pdu_size);
        }

        if (pdu_sizes_.empty()) {
            throw std::runtime_error("No PDU keys were created");
        }

        hakoniwa::pdu::PduKey key = pdu_keys_[0];
        buf_.resize(pdu_sizes_[0]);

        HakoCpp_Twist twist{};
        twist.linear.x = 0.0;
        twist.linear.y = 0.1;
        twist.linear.z = 0.2;
        twist.angular.x = 0.3;
        twist.angular.y = 0.4;
        twist.angular.z = 0.5;

        hako::pdu::msgs::geometry_msgs::Twist twist_convertor;
        send_size_ = twist_convertor.cpp2pdu(
            twist,
            reinterpret_cast<char*>(buf_.data()),
            static_cast<int>(buf_.size()));

        if (send_size_ <= 0) {
            std::cerr
                << "Failed to convert Twist to PDU for key: "
                << key.robot << "/" << key.pdu
                << std::endl;
            throw std::runtime_error("PDU conversion failed");
        }
    }

    std::unique_ptr<hakoniwa::pdu::Endpoint> endpoint_;
    std::vector<hakoniwa::pdu::PduKey> pdu_keys_;
    std::vector<size_t> pdu_sizes_;
    std::vector<std::byte> buf_;
    int send_size_ = 0;
};

}