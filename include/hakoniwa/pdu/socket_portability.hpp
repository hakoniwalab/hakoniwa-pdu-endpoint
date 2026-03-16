#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <string>

namespace hakoniwa {
namespace pdu {

using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;

enum class SocketShutdownMode {
    Read,
    ReadWrite,
};

enum class SocketWaitCondition {
    Readable,
    Writable,
};

bool is_valid_socket(SocketHandle fd) noexcept;
int get_socket_status_flags(SocketHandle fd) noexcept;
HakoPduErrorType set_socket_status_flags(SocketHandle fd, int flags) noexcept;
int last_socket_error() noexcept;
std::string socket_error_message(int error_number);
bool is_socket_would_block(int error_number) noexcept;
bool is_socket_interrupted(int error_number) noexcept;
bool is_socket_connect_in_progress(int error_number) noexcept;
HakoPduErrorType close_socket(SocketHandle fd) noexcept;
void shutdown_socket(SocketHandle fd, SocketShutdownMode mode) noexcept;
HakoPduErrorType set_socket_nonblocking(SocketHandle fd, bool enabled) noexcept;
HakoPduErrorType wait_socket(SocketHandle fd,
                             SocketWaitCondition condition,
                             int timeout_ms,
                             bool& ready) noexcept;

}  // namespace pdu
}  // namespace hakoniwa
