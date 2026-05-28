#include "sub_runner.hpp"
#include "hako_conductor.h"

namespace benchmarks::runner {

void SubShmRunner::on_recv(int recv_event_id)
{
    printf("INFO: on_recv: %d\n", recv_event_id);
    //TODO
}
int SubShmRunner::my_on_initialize(hako_asset_context_t* context)
{
    (void)context;
    //TODO
    return 0;
}

int SubShmRunner::my_on_reset(hako_asset_context_t* context)
{
    (void)context;
    return 0;
}

int SubShmRunner::my_on_manual_timing_control(hako_asset_context_t* context)
{
    (void)context;
    (void)hako_asset_usleep(1000);
    return 0;
}

static hako_asset_callbacks_t my_callback = {
    .on_initialize = SubShmRunner::my_on_initialize,
    .on_manual_timing_control = SubShmRunner::my_on_manual_timing_control,
    .on_simulation_step = NULL,
    .on_reset = SubShmRunner::my_on_reset
};

void SubShmRunner::prepare() {
    set_instance(this);
    open_benchmark_log("sub");
    reset_receive_benchmark(benchmark_config_.try_num);

    hako_conductor_start(benchmark_config_.delta_time_usec, benchmark_config_.max_delay_usec);
    std::string asset_name = "sub_runner_shm_asset";
    std::string pdudef_path = benchmark_config_.benchmark_config_path + "/pdu/pdudefs.json";
    int ret = hako_asset_register(asset_name.c_str(), pdudef_path.c_str(), &my_callback, benchmark_config_.delta_time_usec, HAKO_ASSET_MODEL_PLANT);
    if (ret != 0) {
        throw std::runtime_error("Failed to register asset: " + asset_name);
    }

    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "sub_runner_shm",
        HAKO_PDU_ENDPOINT_DIRECTION_IN);

    std::string endpoint_config_path = benchmark_config_.benchmark_config_path + "/endpoint/subscriber/subscriber_shm.json";
    if (endpoint_->open(endpoint_config_path) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open SHM subscriber endpoint: " + endpoint_config_path);
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
                record_receive_event("shm", received_key, data);
            });
    }

    if (endpoint_->start() != HAKO_PDU_ERR_OK) {
        endpoint_->close();
        endpoint_.reset();
        throw std::runtime_error("Failed to start SHM subscriber endpoint");
    }
}

void SubShmRunner::run() {
    // TODO: Implementation
    int ret = hako_asset_start();
    printf("INFO: hako_asset_start() returns %d\n", ret);
}

void SubShmRunner::cleanup() {
    if (endpoint_) {
        endpoint_->close();
        endpoint_.reset();
    }
    hako_conductor_stop();
}

}
