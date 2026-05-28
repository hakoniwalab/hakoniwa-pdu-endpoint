#pragma once

#include "runner.hpp"
#include "shm_runner.hpp"
#include "hako_asset.h"

namespace benchmarks::runner {

class PubShmRunner : public ShmRunnerBase {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
    static PubShmRunner *instance_;
    static void set_instance(PubShmRunner *inst) { instance_ = inst; }
    static int my_on_initialize(hako_asset_context_t* context);
    static int my_on_reset(hako_asset_context_t* context);
    static int my_on_manual_timing_control(hako_asset_context_t* context);

private:
    void send_benchmark_batch();
    bool sent_{false};
};

class PubTcpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class PubUdpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

}
