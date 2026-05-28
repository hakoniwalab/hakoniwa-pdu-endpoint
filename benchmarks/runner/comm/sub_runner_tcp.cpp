#include "sub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace benchmarks::runner {

void SubTcpRunner::prepare() {
    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "sub_runner_tcp",
        HAKO_PDU_ENDPOINT_DIRECTION_IN);

    std::string endpoint_config_path = benchmark_config_.benchmark_config_path + "/endpoint/subscriber/subscriber_tcp.json";
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open TCP subscriber endpoint: " + endpoint_config_path);
    }

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start TCP subscriber endpoint: " + endpoint_config_path);
    }
    prepare_pdudefs(benchmark_config_.try_num);
    create_send_buffer_for_key(benchmark_config_.try_num);
}

void SubTcpRunner::run() {
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }

    size_t buffer_size = 0;
    if (!pdu_sizes_.empty()) {
        buffer_size = pdu_sizes_[0];
    } else {
        throw std::runtime_error("PDU sizes not prepared");
    }
    std::vector<std::byte> buffer(buffer_size);
    size_t received_size = 0;
    int receive_count = 0;

    auto start_time = std::chrono::steady_clock::now();
    while (receive_count < benchmark_config_.try_num) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        if (elapsed > benchmark_config_.timeout_sec) {
            throw std::runtime_error("Receive timeout");
        }

        for (const auto& key : pdu_keys_) {
            if (endpoint_->recv(key, buffer, received_size) == HAKO_PDU_ERR_OK) {
                receive_count++;
                std::cout << "Received TCP PDU: robot=" << key.robot
                          << " pdu=" << key.pdu
                          << " size=" << received_size
                          << " count=" << receive_count
                          << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SubTcpRunner::cleanup() {
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
}

}
