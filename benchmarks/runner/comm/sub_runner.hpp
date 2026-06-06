#pragma once

#include "runner.hpp"

#include <atomic>

#if defined(HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE)
#include "shm_runner.hpp"
#include "hako_asset.h"
#endif

namespace benchmarks::runner {

#if defined(HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE)
class SubShmRunner : public ShmRunnerBase {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
    static SubShmRunner *instance_;
    static void set_instance(SubShmRunner *inst) { instance_ = inst; }
    static void on_recv(int recv_event_id);
    static int my_on_initialize(hako_asset_context_t* context);
    static int my_on_reset(hako_asset_context_t* context);
    static int my_on_manual_timing_control(hako_asset_context_t* context);
};
#endif

class SubTcpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

class SubUdpRunner : public Runner {
public:
    void prepare() override;
    void run() override;
    void cleanup() override;
};

}
