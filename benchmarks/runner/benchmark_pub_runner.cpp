#include <iostream>
#include "comm/pub_runner.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <endpoint_config_path>" << std::endl;
        return 1;
    }
    std::string endpoint_config_path = argv[1];
    
    benchmarks::runner::PubUdpRunner runner;
    runner.prepare(1, endpoint_config_path);
    runner.run();
    runner.cleanup();

    return 0;
}
