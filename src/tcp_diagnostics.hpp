#pragma once

#include "hakoniwa/pdu/socket_portability.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

namespace hakoniwa {
namespace pdu {
namespace comm {

struct TcpDiagnosticContext {
    const char* transport = "tcp";
    std::string comm_name;
    const char* role = "unknown";
    std::uint64_t connection_id = 0;
    std::string peer;
};

inline std::uint64_t next_tcp_connection_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

inline std::uint64_t tcp_monotonic_msec() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline const char* hako_pdu_error_name(HakoPduErrorType error) noexcept
{
    switch (error) {
        case HAKO_PDU_ERR_OK: return "OK";
        case HAKO_PDU_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case HAKO_PDU_ERR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case HAKO_PDU_ERR_IO_ERROR: return "IO_ERROR";
        case HAKO_PDU_ERR_NO_SPACE: return "NO_SPACE";
        case HAKO_PDU_ERR_BUSY: return "BUSY";
        case HAKO_PDU_ERR_TIMEOUT: return "TIMEOUT";
        case HAKO_PDU_ERR_NO_ENTRY: return "NO_ENTRY";
        case HAKO_PDU_ERR_FILE_NOT_FOUND: return "FILE_NOT_FOUND";
        case HAKO_PDU_ERR_INVALID_JSON: return "INVALID_JSON";
        case HAKO_PDU_ERR_INVALID_CONFIG: return "INVALID_CONFIG";
        case HAKO_PDU_ERR_NOT_RUNNING: return "NOT_RUNNING";
        case HAKO_PDU_ERR_UNSUPPORTED: return "UNSUPPORTED";
        case HAKO_PDU_ERR_INVALID_PDU_KEY: return "INVALID_PDU_KEY";
        case HAKO_PDU_ERR_NOT_OWNER: return "NOT_OWNER";
    }
    return "UNKNOWN";
}

inline std::string trim_socket_message(std::string message)
{
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message;
}

inline std::string format_socket_address(const SocketAddress* address, SocketLength address_len)
{
    if (address == nullptr || address_len <= 0) {
        return {};
    }
    char host[NI_MAXHOST]{};
    char service[NI_MAXSERV]{};
    const int result = ::getnameinfo(
        address,
        address_len,
        host,
        static_cast<SocketLength>(sizeof(host)),
        service,
        static_cast<SocketLength>(sizeof(service)),
        NI_NUMERICHOST | NI_NUMERICSERV);
    if (result != 0) {
        return {};
    }
    return std::string(host) + ":" + service;
}

inline std::mutex& tcp_diagnostic_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline void log_tcp_socket_failure(const TcpDiagnosticContext& context,
                                   const char* operation,
                                   int native_error,
                                   HakoPduErrorType mapped_error,
                                   int timeout_ms,
                                   bool stopping)
{
    if (stopping) {
        return;
    }
    std::lock_guard<std::mutex> lock(tcp_diagnostic_mutex());
    std::cerr << "TCP transport error"
              << " timestamp_msec=" << tcp_monotonic_msec()
              << " transport=" << context.transport
              << " comm=" << (context.comm_name.empty() ? "unknown" : context.comm_name)
              << " role=" << context.role
              << " connection_id=" << context.connection_id
              << " peer=" << (context.peer.empty() ? "unknown" : context.peer)
              << " operation=" << operation
              << " native_error=" << native_error
              << " native_message=\"" << trim_socket_message(socket_error_message(native_error)) << "\""
              << " mapped_error=" << hako_pdu_error_name(mapped_error)
              << " timeout_ms=" << timeout_ms
              << std::endl;
}

inline void log_tcp_mapped_failure(const TcpDiagnosticContext& context,
                                   const char* operation,
                                   HakoPduErrorType mapped_error,
                                   int timeout_ms,
                                   const char* detail,
                                   bool stopping)
{
    if (stopping) {
        return;
    }
    std::lock_guard<std::mutex> lock(tcp_diagnostic_mutex());
    std::cerr << "TCP transport error"
              << " timestamp_msec=" << tcp_monotonic_msec()
              << " transport=" << context.transport
              << " comm=" << (context.comm_name.empty() ? "unknown" : context.comm_name)
              << " role=" << context.role
              << " connection_id=" << context.connection_id
              << " peer=" << (context.peer.empty() ? "unknown" : context.peer)
              << " operation=" << operation
              << " native_error=unavailable"
              << " mapped_error=" << hako_pdu_error_name(mapped_error)
              << " timeout_ms=" << timeout_ms
              << " detail=\"" << detail << "\""
              << std::endl;
}

inline void log_tcp_peer_closed(const TcpDiagnosticContext& context,
                                const char* operation,
                                bool stopping)
{
    if (stopping) {
        return;
    }
    std::lock_guard<std::mutex> lock(tcp_diagnostic_mutex());
    std::cerr << "TCP peer closed"
              << " timestamp_msec=" << tcp_monotonic_msec()
              << " transport=" << context.transport
              << " comm=" << (context.comm_name.empty() ? "unknown" : context.comm_name)
              << " role=" << context.role
              << " connection_id=" << context.connection_id
              << " peer=" << (context.peer.empty() ? "unknown" : context.peer)
              << " operation=" << operation
              << " mapped_error=IO_ERROR"
              << std::endl;
}

inline void log_tcp_protocol_error(const TcpDiagnosticContext& context,
                                   const char* operation,
                                   const std::string& detail,
                                   bool stopping)
{
    if (stopping) {
        return;
    }
    std::lock_guard<std::mutex> lock(tcp_diagnostic_mutex());
    std::cerr << "TCP protocol error"
              << " timestamp_msec=" << tcp_monotonic_msec()
              << " transport=" << context.transport
              << " comm=" << (context.comm_name.empty() ? "unknown" : context.comm_name)
              << " role=" << context.role
              << " connection_id=" << context.connection_id
              << " peer=" << (context.peer.empty() ? "unknown" : context.peer)
              << " operation=" << operation
              << " detail=\"" << detail << "\""
              << std::endl;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
