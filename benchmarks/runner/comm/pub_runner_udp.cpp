#include "pub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include <iostream>
#include <vector>
#include <numeric>

namespace benchmarks::runner {

void PubUdpRunner::prepare(int num, std::string endpoint_config_path) {
    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>("pub_runner_udp", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open UDP publisher endpoint" << std::endl;
        // In a real scenario, proper error handling (e.g., throwing an exception) would be better.
        return;
    }
    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start UDP publisher endpoint" << std::endl;
        // Proper error handling.
        return;
    }
    create_send_buffer_for_key(num);
}

void PubUdpRunner::run() {
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }
    int num = static_cast<int>(pdu_keys_.size());
    for (int i = 0; i < num; ++i) {
        if (endpoint_->send(pdu_keys_[i], std::span<const std::byte>(buf_.data(), static_cast<size_t>(send_size_))) != HAKO_PDU_ERR_OK) {
            std::cerr << "Failed to send PDU for key: " << pdu_keys_[i].robot << "/" << pdu_keys_[i].pdu << std::endl;
            throw std::runtime_error("Failed to send PDU");
        }
    }
}

void PubUdpRunner::cleanup() {
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
}

}
