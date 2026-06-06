#pragma once

#include "runner.hpp"

#if defined(HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE)
#include "shm_runner.hpp"
#include "hako_asset.h"
#endif

namespace benchmarks::runner {

#if defined(HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE)
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
#endif

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
