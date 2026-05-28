#include <iostream>
#include "comm/sub_runner.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <benchmark_config_path>" << std::endl;
        return 1;
    }
    std::string benchmark_config_path = argv[1];
    
    benchmarks::runner::SubUdpRunner runner;
    try {
        runner.load_benchmark_config(benchmark_config_path);
        runner.prepare();
        runner.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    runner.cleanup();
    return 0;
}
