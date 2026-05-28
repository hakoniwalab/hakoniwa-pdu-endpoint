#include "sub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace benchmarks::runner {

void SubUdpRunner::prepare()
{
    open_benchmark_log("sub");
    reset_receive_benchmark(benchmark_config_.try_num);

    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "sub_runner_udp",
        HAKO_PDU_ENDPOINT_DIRECTION_IN);

    std::string endpoint_config_path = benchmark_config_.benchmark_config_path + "/endpoint/subscriber/subscriber_udp.json";
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open UDP subscriber endpoint: " + endpoint_config_path);
    }
    prepare_pdudefs(benchmark_config_.try_num);
    for (int i = 0; i < benchmark_config_.try_num; ++i) {
        hakoniwa::pdu::PduKey key = {"Drone-" + std::to_string(i + 1), benchmark_config_.pdu_name};
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
                record_receive_event("udp", received_key, data);
            });
    }

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start UDP subscriber endpoint");
    }
}

void SubUdpRunner::run()
{
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }

    std::ostringstream oss;
    oss << "BENCH_SUB_WAIT protocol=udp expected=" << expected_count_.load()
        << " timeout_sec=" << benchmark_config_.timeout_sec;
    write_benchmark_log(oss.str());
    wait_receive_benchmark("udp");
}

void SubUdpRunner::cleanup()
{
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
    close_benchmark_log();
}

}
