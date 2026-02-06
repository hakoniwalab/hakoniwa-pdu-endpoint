#pragma once

#include "hakoniwa/pdu/comm/comm_mux.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace hakoniwa {
namespace pdu {
namespace comm {

// TCP mux comm: accept multiple TCP clients and expose each as a session comm.
// Sessions are consumed via take_sessions() by the EndpointCommMultiplexer.
class TcpCommMultiplexer final : public CommMultiplexer
{
public:
    TcpCommMultiplexer();
    ~TcpCommMultiplexer();

    HakoPduErrorType open(const std::string& config_path) override;
    HakoPduErrorType close() noexcept override;
    HakoPduErrorType start() noexcept override;
    HakoPduErrorType stop() noexcept override;

    std::vector<std::shared_ptr<PduComm>> take_sessions() override;

    size_t connected_count() const noexcept override;
    size_t expected_count() const noexcept override;

private:
    void accept_loop_();

    struct Options {
        int backlog = 5;
        int read_timeout_ms = 1000;
        int write_timeout_ms = 1000;
        bool blocking = true;
        bool reuse_address = true;
        bool keepalive = true;
        bool no_delay = true;
        int recv_buffer_size = 8192;
        int send_buffer_size = 8192;
        bool linger_enabled = false;
        int linger_timeout_sec = 0;
    };

    HakoPduErrorType configure_socket_options_(int fd, const Options& options) noexcept;

    std::atomic<int> listen_fd_{-1};
    std::atomic<bool> is_running_{false};
    std::thread accept_thread_;

    Options options_{};
    size_t expected_clients_ = 0;
    std::atomic<size_t> connected_clients_{0};

    mutable std::mutex sessions_mutex_;
    std::vector<std::shared_ptr<PduComm>> pending_sessions_;
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
