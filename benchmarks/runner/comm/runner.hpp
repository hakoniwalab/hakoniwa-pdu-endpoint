#pragma once

#include <string>
#include <vector>
#include <memory>
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
        for (int i = 0; i < num; ++i) {
            hakoniwa::pdu::PduKey key = {"Drone-" + std::to_string(i), "pos"};
            size_t pdu_size = endpoint_->get_pdu_size(key);
            if (pdu_size == 0) {
                throw std::runtime_error("Failed to get PDU size for key: " + key.robot + "/" + key.pdu);
            }
            pdu_keys_.push_back(key);
            pdu_sizes_.push_back(pdu_size);
        }
        hakoniwa::pdu::PduKey key = pdu_keys_[0];
        std::vector<std::byte> buf(pdu_sizes_[0]);
        HakoCpp_Twist twist;
        twist.linear.x = static_cast<double>(0);
        twist.linear.y = static_cast<double>(0) + 0.1;
        twist.linear.z = static_cast<double>(0) + 0.2;
        twist.angular.x = static_cast<double>(0) + 0.3;
        twist.angular.y = static_cast<double>(0) + 0.4;
        twist.angular.z = static_cast<double>(0) + 0.5;
        hako::pdu::msgs::geometry_msgs::Twist twist_convertor;
        send_size_ = twist_convertor.cpp2pdu(twist,
                                    reinterpret_cast<char*>(buf_.data()),
                                    static_cast<int>(buf_.size()));
        if (send_size_ <= 0) {
            std::cerr << "Failed to convert Twist to PDU for key: " << key.robot << "/" << key.pdu << std::endl;
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