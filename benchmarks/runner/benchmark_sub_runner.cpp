#include <iostream>
#include "comm/sub_runner.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <endpoint_config_path>" << std::endl;
        return 1;
    }
    std::string endpoint_config_path = argv[1];
    
    benchmarks::runner::SubUdpRunner runner;
    try {
        runner.prepare(1, endpoint_config_path);
        runner.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    runner.cleanup();
    return 0;
}
