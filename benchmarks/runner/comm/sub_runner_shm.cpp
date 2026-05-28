#include "sub_runner.hpp"
#include "hako_conductor.h"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace benchmarks::runner {

namespace {

std::filesystem::path generate_shm_subscriber_config(
    const std::string& config_path,
    int try_num)
{
    const std::filesystem::path generated_root =
        std::filesystem::path(config_path) / "generated" / "shm";
    const std::filesystem::path endpoint_dir =
        generated_root / "endpoint" / "subscriber";
    const std::filesystem::path comm_dir = endpoint_dir / "comm";

    std::filesystem::create_directories(comm_dir);

    nlohmann::json comm_config;
    comm_config["protocol"] = "shm";
    comm_config["impl_type"] = "callback";
    comm_config["name"] = "shm_subscriber";
    comm_config["direction"] = "inout";
    comm_config["io"]["robots"] = nlohmann::json::array();

    for (int i = 0; i < try_num; ++i) {
        nlohmann::json pdu = nlohmann::json::array({
            {
                {"name", "pos"},
                {"notify_on_recv", true}
            }
        });
        comm_config["io"]["robots"].push_back({
            {"name", "Drone-" + std::to_string(i + 1)},
            {"pdu", pdu}
        });
    }

    const std::filesystem::path comm_config_path =
        comm_dir / "subscriber_shm_comm.json";
    {
        std::ofstream ofs(comm_config_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            throw std::runtime_error(
                "Failed to open generated SHM comm config: " + comm_config_path.string());
        }
        ofs << comm_config.dump(2) << std::endl;
    }

    nlohmann::json endpoint_config;
    endpoint_config["name"] = "ep_shm_subscriber";
    endpoint_config["cache"] = "../../../../endpoint/cache/buffer.json";
    endpoint_config["comm"] = "comm/subscriber_shm_comm.json";

    const std::filesystem::path endpoint_config_path =
        endpoint_dir / "subscriber_shm.json";
    {
        std::ofstream ofs(endpoint_config_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            throw std::runtime_error(
                "Failed to open generated SHM endpoint config: " + endpoint_config_path.string());
        }
        ofs << endpoint_config.dump(2) << std::endl;
    }

    return endpoint_config_path;
}

} // namespace

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

    const auto endpoint_config_path = generate_shm_subscriber_config(
        benchmark_config_.benchmark_config_path,
        benchmark_config_.try_num);

    hako_conductor_start(benchmark_config_.delta_time_usec, benchmark_config_.max_delay_usec);
    std::string asset_name = "sub_runner_shm_asset";
    std::string pdudef_path = benchmark_config_.benchmark_config_path + "/pdu/pdudef.json";
    int ret = hako_asset_register(asset_name.c_str(), pdudef_path.c_str(), &my_callback, benchmark_config_.delta_time_usec, HAKO_ASSET_MODEL_PLANT);
    if (ret != 0) {
        throw std::runtime_error("Failed to register asset: " + asset_name);
    }

    endpoint_ = std::make_unique<hakoniwa::pdu::Endpoint>(
        "sub_runner_shm",
        HAKO_PDU_ENDPOINT_DIRECTION_IN);

    if (endpoint_->open(endpoint_config_path.string()) != HAKO_PDU_ERR_OK) {
        endpoint_.reset();
        throw std::runtime_error("Failed to open SHM subscriber endpoint: " + endpoint_config_path.string());
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
    int ret = hako_asset_start();
    printf("INFO: hako_asset_start() returns %d\n", ret);
}

void SubShmRunner::cleanup() {
    close_benchmark_log();
    if (endpoint_) {
        endpoint_->close();
        endpoint_.reset();
    }
    hako_conductor_stop();
}

}
