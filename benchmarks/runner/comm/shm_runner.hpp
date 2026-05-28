#pragma once

#include "runner.hpp"

#include <filesystem>
#include <string>

namespace benchmarks::runner {

class ShmRunnerBase : public Runner {
protected:
    std::filesystem::path generated_shm_pdudef_path() const;
    std::filesystem::path generate_shm_endpoint_config(
        const std::string& role,
        const std::string& endpoint_name,
        const std::string& comm_name,
        bool notify_on_recv);
};

}
