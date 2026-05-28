#include "pub_runner.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace benchmarks::runner {

PubShmRunner* PubShmRunner::instance_ = nullptr;

int PubShmRunner::my_on_initialize(hako_asset_context_t* context)
{
    (void)context;
    std::cout << "INFO: PubShmRunner::my_on_initialize called" << std::endl;
    if (instance_ == nullptr) {
        throw std::runtime_error("PubShmRunner instance is not set in on_initialize");
    }
    return 0;
}

int PubShmRunner::my_on_reset(hako_asset_context_t* context)
{
    (void)context;
    if (instance_ != nullptr) {
        instance_->sent_ = false;
    }
    return 0;
}

void PubShmRunner::send_benchmark_batch()
{
    if (!endpoint_) {
        throw std::runtime_error("Endpoint not initialized");
    }
    if (sent_) {
        return;
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
        oss << "BENCH_PUB_EVENT protocol=shm"
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
    oss << "BENCH_PUB_SUMMARY protocol=shm"
        << " expected=" << benchmark_config_.try_num
        << " sent=" << sent_count
        << " send_start_ns=" << send_start_ns
        << " send_end_ns=" << send_end_ns
        << " send_duration_ms=" << send_duration_ms;
    write_benchmark_log(oss.str());
    sent_ = true;
    flush_benchmark_log();
}

int PubShmRunner::my_on_manual_timing_control(hako_asset_context_t* context)
{
    (void)context;
    if (instance_ == nullptr) {
        throw std::runtime_error("PubShmRunner instance is not set in on_manual_timing_control");
    }

    instance_->send_benchmark_batch();

    while (true) {
        int ret = hako_asset_usleep(instance_->benchmark_config_.delta_time_usec);
        if (ret != 0) {
            std::cout << "INFO: hako_asset_usleep() returned " << ret
                      << ", stopping publisher timing control." << std::endl;
            break;
        }
    }
    return 0;
}

static hako_asset_callbacks_t create_shm_callbacks()
{
    hako_asset_callbacks_t callbacks{};
    callbacks.on_initialize = PubShmRunner::my_on_initialize;
    callbacks.on_manual_timing_control = PubShmRunner::my_on_manual_timing_control;
    callbacks.on_reset = PubShmRunner::my_on_reset;
    return callbacks;
}

static hako_asset_callbacks_t my_callback = {};

void PubShmRunner::prepare() {
    my_callback = create_shm_callbacks();
    set_instance(this);
    sent_ = false;
    open_benchmark_log("pub");

    const auto endpoint_config_path = generate_shm_endpoint_config(
        "publisher",
        "ep_shm_publisher",
        "shm_publisher",
        false);

    std::string asset_name = "pub_runner_shm_asset";
    std::string pdudef_path = generated_shm_pdudef_path().string();
    int ret = hako_asset_register(asset_name.c_str(), pdudef_path.c_str(), &my_callback, benchmark_config_.delta_time_usec, HAKO_ASSET_MODEL_PLANT);
    if (ret != 0) {
        throw std::runtime_error("Failed to register asset: " + asset_name);
    }

    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "pub_runner_shm",
        HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    if (endpoint_->open(endpoint_config_path.string()) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open SHM publisher endpoint: " + endpoint_config_path.string());
    }
    prepare_pdudefs(benchmark_config_.try_num);
    create_send_buffer_for_key(benchmark_config_.try_num);

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start SHM publisher endpoint");
    }
}

void PubShmRunner::run() {
    int ret = hako_asset_start();
    printf("INFO: hako_asset_start() returns %d\n", ret);
}

void PubShmRunner::cleanup() {
    close_benchmark_log();
    if (endpoint_) {
        endpoint_->stop();
        endpoint_->close();
        endpoint_.reset();
    }
    set_instance(nullptr);
}

}
