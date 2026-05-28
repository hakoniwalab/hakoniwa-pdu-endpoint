#include "pub_runner.hpp"
#include "hakoniwa/pdu/endpoint.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace benchmarks::runner {

void PubUdpRunner::prepare()
{
    open_benchmark_log("pub");

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

void PubUdpRunner::run()
{
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }

    int sent_count = 0;
    const auto send_start_ns = now_ns();
    for (int i = 0; i < benchmark_config_.try_num; ++i) {
        if (endpoint_->send(pdu_keys_[i], std::span<const std::byte>(buf_.data(), static_cast<size_t>(send_size_))) != HAKO_PDU_ERR_OK) {
            std::cerr << "Failed to send PDU for key: " << pdu_keys_[i].robot << "/" << pdu_keys_[i].pdu << std::endl;
            throw std::runtime_error("Failed to send PDU for key: " + pdu_keys_[i].robot + "/" + pdu_keys_[i].pdu);
        }
        ++sent_count;
        std::ostringstream oss;
        oss << "BENCH_PUB_EVENT protocol=udp"
            << " robot=" << pdu_keys_[i].robot
            << " pdu=" << pdu_keys_[i].pdu
            << " size=" << send_size_
            << " count=" << sent_count
            << " send_ns=" << now_ns();
        write_benchmark_log(oss.str());
    }
    const auto send_end_ns = now_ns();
    const double send_duration_ms = static_cast<double>(send_end_ns - send_start_ns) / 1000000.0;
    std::ostringstream oss;
    oss << "BENCH_PUB_SUMMARY protocol=udp"
        << " expected=" << benchmark_config_.try_num
        << " sent=" << sent_count
        << " send_start_ns=" << send_start_ns
        << " send_end_ns=" << send_end_ns
        << " send_duration_ms=" << send_duration_ms;
    write_benchmark_log(oss.str());
}

void PubUdpRunner::cleanup()
{
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
    close_benchmark_log();
}

}
