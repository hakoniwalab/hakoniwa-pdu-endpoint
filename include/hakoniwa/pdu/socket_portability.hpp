#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <BaseTsd.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace hakoniwa {
namespace pdu {

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketSize = SSIZE_T;
using SocketLength = int;
#else
using SocketHandle = int;
using SocketSize = ssize_t;
using SocketLength = socklen_t;
#endif

using SocketAddress = sockaddr;
using SocketAddressStorage = sockaddr_storage;
using AddressInfo = addrinfo;

inline constexpr SocketHandle kInvalidSocket =
#ifdef _WIN32
    INVALID_SOCKET;
#else
    -1;
#endif

inline bool to_socket_length(size_t n, SocketLength& out) noexcept
{
    if (n > static_cast<size_t>(std::numeric_limits<SocketLength>::max())) {
        return false;
    }
    out = static_cast<SocketLength>(n);
    return true;
}

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
// True only for a native blocking-socket timeout whose connection must no
// longer be reused. On Winsock this is WSAETIMEDOUT. POSIX EAGAIN/EWOULDBLOCK
// remains classified as would-block because the native error does not
// distinguish a configured timeout from non-blocking readiness.
bool is_socket_timeout(int error_number) noexcept;
bool is_socket_interrupted(int error_number) noexcept;
bool is_socket_connect_in_progress(int error_number) noexcept;
HakoPduErrorType close_socket(SocketHandle fd) noexcept;
void shutdown_socket(SocketHandle fd, SocketShutdownMode mode) noexcept;
HakoPduErrorType set_socket_nonblocking(SocketHandle fd, bool enabled) noexcept;
SocketHandle create_socket(int family, int type, int protocol) noexcept;
SocketHandle accept_socket(SocketHandle fd, SocketAddress* address, SocketLength* address_len) noexcept;
int connect_socket(SocketHandle fd, const SocketAddress* address, SocketLength address_len) noexcept;
int bind_socket(SocketHandle fd, const SocketAddress* address, SocketLength address_len) noexcept;
int listen_socket(SocketHandle fd, int backlog) noexcept;
SocketSize recv_socket(SocketHandle fd, std::byte* buffer, size_t size, int flags) noexcept;
SocketSize send_socket(SocketHandle fd, const std::byte* buffer, size_t size, int flags) noexcept;
SocketSize recv_from_socket(SocketHandle fd,
                            std::byte* buffer,
                            size_t size,
                            int flags,
                            SocketAddress* address,
                            SocketLength* address_len) noexcept;
SocketSize send_to_socket(SocketHandle fd,
                          const std::byte* buffer,
                          size_t size,
                          int flags,
                          const SocketAddress* address,
                          SocketLength address_len) noexcept;
HakoPduErrorType set_socket_option_int(SocketHandle fd, int level, int optname, int value) noexcept;
HakoPduErrorType get_socket_option_int(SocketHandle fd, int level, int optname, int& value) noexcept;
HakoPduErrorType set_socket_option_buffer(SocketHandle fd,
                                          int level,
                                          int optname,
                                          const void* value,
                                          SocketLength value_len) noexcept;
HakoPduErrorType set_socket_timeout_option(SocketHandle fd, int optname, int timeout_ms) noexcept;
HakoPduErrorType set_socket_linger_option(SocketHandle fd, bool enabled, int timeout_sec) noexcept;
void free_address_info(AddressInfo* info) noexcept;
HakoPduErrorType wait_socket(SocketHandle fd,
                             SocketWaitCondition condition,
                             int timeout_ms,
                             bool& ready) noexcept;

}  // namespace pdu
}  // namespace hakoniwa
