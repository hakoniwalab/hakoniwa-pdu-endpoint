#include "sub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace benchmarks::runner {

void SubTcpRunner::prepare() {
    expected_count_.store(benchmark_config_.try_num);
    received_count_.store(0);

    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "sub_runner_tcp",
        HAKO_PDU_ENDPOINT_DIRECTION_IN);

    std::string endpoint_config_path = benchmark_config_.benchmark_config_path + "/endpoint/subscriber/subscriber_tcp.json";
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open TCP subscriber endpoint: " + endpoint_config_path);
    }
    prepare_pdudefs(benchmark_config_.try_num);
    for (int i = 0; i < benchmark_config_.try_num; ++i) {
        hakoniwa::pdu::PduKey key = {"Drone-" + std::to_string(i + 1), "pos"};
        const auto channel_id = endpoint_->get_pdu_channel_id(key);
        if (channel_id < 0) {
            endpoint_->close();
            endpoint_.reset();
            throw std::runtime_error(
                "Failed to get PDU channel ID for key: " + key.robot + "/" + key.pdu);
        }

        hakoniwa::pdu::PduResolvedKey resolved_key = {key.robot, channel_id};
        endpoint_->subscribe_on_recv_callback(
            resolved_key,
            [this](const hakoniwa::pdu::PduResolvedKey& received_key,
                   std::span<const std::byte> data) {
                const int count = received_count_.fetch_add(1) + 1;
                std::cout
                    << "Received TCP PDU: robot=" << received_key.robot
                    << " channel=" << received_key.channel_id
                    << " size=" << data.size()
                    << " count=" << count
                    << std::endl;
            });
    }

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start TCP subscriber endpoint");
    }
}

void SubTcpRunner::run() {
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }

    int max_wait_ms = benchmark_config_.timeout_sec * 1000;
    constexpr int sleep_ms = 10;
    std::cout << "Waiting for TCP PDUs: expected=" << expected_count_.load() << " timeout=" << benchmark_config_.timeout_sec << " seconds" << std::endl;
    for (int elapsed_ms = 0; elapsed_ms < max_wait_ms; elapsed_ms += sleep_ms) {
        if (received_count_.load() >= expected_count_.load()) {
            return;
        }
        endpoint_->process_recv_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    std::cerr
        << "Timed out waiting for TCP PDUs: received=" << received_count_.load()
        << " expected=" << expected_count_.load()
        << std::endl;
}

void SubTcpRunner::cleanup() {
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
}

}
