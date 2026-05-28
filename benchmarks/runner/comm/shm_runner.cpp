#include "shm_runner.hpp"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace benchmarks::runner {

std::filesystem::path ShmRunnerBase::generate_shm_endpoint_config(
    const std::string& role,
    const std::string& endpoint_name,
    const std::string& comm_name,
    bool notify_on_recv)
{
    const std::filesystem::path generated_root =
        std::filesystem::path(benchmark_config_.benchmark_config_path) / "generated" / "shm";
    const std::filesystem::path endpoint_dir =
        generated_root / "endpoint" / role;
    const std::filesystem::path comm_dir = endpoint_dir / "comm";

    std::filesystem::create_directories(comm_dir);

    nlohmann::json comm_config;
    comm_config["protocol"] = "shm";
    comm_config["impl_type"] = "callback";
    comm_config["name"] = comm_name;
    comm_config["direction"] = "inout";
    comm_config["io"]["robots"] = nlohmann::json::array();

    for (int i = 0; i < benchmark_config_.try_num; ++i) {
        nlohmann::json pdu_item;
        pdu_item["name"] = "pos";
        if (notify_on_recv) {
            pdu_item["notify_on_recv"] = true;
        }

        comm_config["io"]["robots"].push_back({
            {"name", "Drone-" + std::to_string(i + 1)},
            {"pdu", nlohmann::json::array({pdu_item})}
        });
    }

    const std::filesystem::path comm_config_path =
        comm_dir / (role + std::string("_shm_comm.json"));
    {
        std::ofstream ofs(comm_config_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            throw std::runtime_error(
                "Failed to open generated SHM comm config: " + comm_config_path.string());
        }
        ofs << comm_config.dump(2) << std::endl;
    }

    nlohmann::json endpoint_config;
    endpoint_config["name"] = endpoint_name;
    endpoint_config["cache"] = "../../../../endpoint/cache/buffer.json";
    endpoint_config["comm"] = "comm/" + role + std::string("_shm_comm.json");

    const std::filesystem::path endpoint_config_path =
        endpoint_dir / (role + std::string("_shm.json"));
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

}
