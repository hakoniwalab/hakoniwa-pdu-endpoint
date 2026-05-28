#include "pub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"

#include <iostream>
#include <stdexcept>

namespace benchmarks::runner {

void PubUdpRunner::prepare()
{
    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "pub_runner_udp",
        HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    std::string endpoint_config_path = benchmark_config_.benchmark_config_path + "/endpoint/publisher/publisher_udp.json";
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open UDP publisher endpoint: " + endpoint_config_path);
    }

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start UDP publisher endpoint: " + endpoint_config_path);
    }
    prepare_pdudefs(benchmark_config_.try_num);
    create_send_buffer_for_key(benchmark_config_.try_num);
}

void PubUdpRunner::run() {
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }
    int num = static_cast<int>(pdu_keys_.size());
    for (int i = 0; i < benchmark_config_.try_num; ++i) {
        if (endpoint_->send(pdu_keys_[i], std::span<const std::byte>(buf_.data(), static_cast<size_t>(send_size_))) != HAKO_PDU_ERR_OK) {
            std::cerr << "Failed to send PDU for key: " << pdu_keys_[i].robot << "/" << pdu_keys_[i].pdu << std::endl;
            throw std::runtime_error("Failed to send PDU for key: " + pdu_keys_[i].robot + "/" + pdu_keys_[i].pdu);
        }
        std::cout << "Sent UDP PDU: robot=" << pdu_keys_[i].robot
                  << " channel=" << pdu_keys_[i].pdu
                  << " size=" << send_size_
                  << " count=" << (i + 1)
                  << std::endl;
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
