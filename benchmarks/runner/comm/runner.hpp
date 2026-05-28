#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>

#include "nlohmann/json.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include "geometry_msgs/pdu_cpptype_conv_Twist.hpp"

namespace benchmarks::runner {

enum class CommType {
    SHM,
    TCP,
    UDP
};
struct BenchmarkConfig {
    std::string benchmark_config_path;
    CommType comm_type;
    int try_num;
    int timeout_sec;
};

class Runner {
public:
    virtual ~Runner() = default;
    void load_benchmark_config(const std::string& config_path)
    {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Failed to open benchmark config file: " + config_path);
        }

        nlohmann::json config;
        try {
            ifs >> config;
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("JSON parsing failed for benchmark config: " + config_path + ". Details: " + e.what());
        }

        try {
            if (config.contains("protocol")) {
                std::string protocol = config["protocol"].get<std::string>();
                if (protocol == "shm") {
                    benchmark_config_.comm_type = CommType::SHM;
                } else if (protocol == "tcp") {
                    benchmark_config_.comm_type = CommType::TCP;
                } else if (protocol == "udp") {
                    benchmark_config_.comm_type = CommType::UDP;
                } else {
                    throw std::runtime_error("Unknown protocol in benchmark config: " + protocol);
                }
            } else {
                throw std::runtime_error("Benchmark config missing 'protocol': " + config_path);
            }

            if (config.contains("try_num")) {
                benchmark_config_.try_num = config["try_num"].get<int>();
            } else {
                throw std::runtime_error("Benchmark config missing 'try_num': " + config_path);
            }
            if (config.contains("config_path")) {
                benchmark_config_.benchmark_config_path = config["config_path"].get<std::string>();
            } else {
                throw std::runtime_error("Benchmark config missing 'config_path': " + config_path);
            }
            if (config.contains("timeout_sec")) {
                benchmark_config_.timeout_sec = config["timeout_sec"].get<int>();
            }
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("JSON access failed for benchmark config: " + config_path + ". Details: " + e.what());
        }
    }
    virtual void prepare() = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

protected:
    void prepare_pdudefs(int num)
    {
        auto pdu_def = endpoint_->get_pdu_definition();
        if (!pdu_def) {
            throw std::runtime_error("PDU definition is not available in endpoint");
        }
        hakoniwa::pdu::PduKey key_org = {"Drone-1", "pos"};
        size_t pdu_size = endpoint_->get_pdu_size(key_org);
        int channel_id = endpoint_->get_pdu_channel_id(key_org);
        if (pdu_size == 0) {
            throw std::runtime_error(
                "Failed to get PDU size for key: " + key_org.robot + "/" + key_org.pdu);
        }
        for (int i = 1; i < num; ++i) {
            pdu_def->add_definition("Drone-" + std::to_string(i + 1), {
                .type = "geometry_msgs/Twist",
                .org_name = key_org.pdu,
                .channel_id = channel_id,
                .pdu_size = pdu_size
            });
        }
    }
    void create_send_buffer_for_key(int num)
    {
        pdu_keys_.clear();
        pdu_sizes_.clear();
        buf_.clear();
        send_size_ = 0;
        for (int i = 0; i < benchmark_config_.try_num; ++i) {
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
    BenchmarkConfig benchmark_config_;
};

}