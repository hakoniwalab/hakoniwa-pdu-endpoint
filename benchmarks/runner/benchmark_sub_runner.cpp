#include <iostream>
#include <memory>
#include <fstream>
#include "nlohmann/json.hpp"
#include "comm/sub_runner.hpp"
#include "comm/runner.hpp"

struct CliOptions {
    std::string benchmark_config_path;
    std::string pdu_name_override;
};

CliOptions parse_args(int argc, char* argv[])
{
    if (argc < 2 || argc > 4) {
        throw std::runtime_error(
            std::string("Usage: ") + argv[0] +
            " <benchmark_config_path> [--pdu-name <name>|<pdu_name>]");
    }

    CliOptions options;
    options.benchmark_config_path = argv[1];
    if (argc == 3) {
        options.pdu_name_override = argv[2];
    } else if (argc == 4) {
        if (std::string(argv[2]) != "--pdu-name") {
            throw std::runtime_error(
                std::string("Usage: ") + argv[0] +
                " <benchmark_config_path> [--pdu-name <name>|<pdu_name>]");
        }
        options.pdu_name_override = argv[3];
    }
    return options;
}

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
    std::unique_ptr<benchmarks::runner::Runner> runner;

    try {
        const auto options = parse_args(argc, argv);
        benchmarks::runner::CommType comm_type = get_comm_type(options.benchmark_config_path);

        if (comm_type == benchmarks::runner::CommType::UDP) {
            runner = std::make_unique<benchmarks::runner::SubUdpRunner>();
        } else if (comm_type == benchmarks::runner::CommType::TCP) {
            runner = std::make_unique<benchmarks::runner::SubTcpRunner>();
        } else if (comm_type == benchmarks::runner::CommType::SHM) {
            runner = std::make_unique<benchmarks::runner::SubShmRunner>();
        } else {
            throw std::runtime_error("Unsupported protocol for sub runner");
        }

        runner->load_benchmark_config(options.benchmark_config_path, options.pdu_name_override);
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
