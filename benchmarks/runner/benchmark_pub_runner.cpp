#include <iostream>
#include <memory>
#include <fstream>
#include "nlohmann/json.hpp"
#include "comm/pub_runner.hpp"
#include "comm/runner.hpp"

// Helper to determine communication type from config
benchmarks::runner::CommType get_comm_type(const std::string& config_path) {
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open benchmark config file: " + config_path);
    }
    nlohmann::json config;
    ifs >> config;
    std::string protocol = config.value("protocol", "udp");
    if (protocol == "udp") return benchmarks::runner::CommType::UDP;
    if (protocol == "tcp") return benchmarks::runner::CommType::TCP;
    if (protocol == "shm") return benchmarks::runner::CommType::SHM;
    throw std::runtime_error("Unknown or unsupported protocol: " + protocol);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <benchmark_config_path>" << std::endl;
        return 1;
    }
    std::string benchmark_config_path = argv[1];
    
    std::unique_ptr<benchmarks::runner::Runner> runner;

    try {
        benchmarks::runner::CommType comm_type = get_comm_type(benchmark_config_path);

        if (comm_type == benchmarks::runner::CommType::UDP) {
            runner = std::make_unique<benchmarks::runner::PubUdpRunner>();
        } else if (comm_type == benchmarks::runner::CommType::TCP) {
            runner = std::make_unique<benchmarks::runner::PubTcpRunner>();
        } else {
            throw std::runtime_error("Unsupported protocol for pub runner");
        }

        runner->load_benchmark_config(benchmark_config_path);
        runner->prepare();
        runner->run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    if (runner) {
        runner->cleanup();
    }

    return 0;
}
