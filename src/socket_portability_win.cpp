#include "hakoniwa/pdu/socket_portability.hpp"

#ifdef _WIN32

#include <cstddef>
#include <string>

namespace hakoniwa {
namespace pdu {

// A simple RAII wrapper for WSAStartup
namespace {
    class WsaInitializer {
    public:
        WsaInitializer() {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                // In a real application, you might throw an exception
                // or handle this more gracefully.
            }
        }
        ~WsaInitializer() {
            WSACleanup();
        }
    };
    // This global instance ensures WSAStartup is called once when the library loads
    // and WSACleanup is called when it unloads.
    const WsaInitializer wsa_initializer;
}

bool is_valid_socket(SocketHandle fd) noexcept
{
    return fd != INVALID_SOCKET;
}

int get_socket_status_flags(SocketHandle fd) noexcept
{
    u_long mode = 0;
    return (::ioctlsocket(fd, FIONBIO, &mode) == 0 && mode != 0) ? 1 : 0;
}

HakoPduErrorType set_socket_status_flags(SocketHandle fd, int flags) noexcept
{
    return set_socket_nonblocking(fd, flags != 0);
}


int last_socket_error() noexcept
{
    return WSAGetLastError();
}

std::string socket_error_message(int error_number)
{
    char* s = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_number,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&s,
        0,
        nullptr
    );
    std::string msg(s);
    LocalFree(s);
    return msg;
}

bool is_socket_would_block(int error_number) noexcept
{
    // POSIX reports SO_RCVTIMEO/SO_SNDTIMEO expiry as EAGAIN/EWOULDBLOCK,
    // while Winsock reports the equivalent blocking-socket timeout as WSAETIMEDOUT.
    // Normalize both cases as retryable so higher-level TCP/TCP-mux/UDP loops behave
    // consistently across platforms.
    return error_number == WSAEWOULDBLOCK || error_number == WSAETIMEDOUT;
}

bool is_socket_interrupted(int error_number) noexcept
{
    return error_number == WSAEINTR;
}

bool is_socket_connect_in_progress(int error_number) noexcept
{
    // For non-blocking sockets, connect returns an error, and WSAEWOULDBLOCK indicates it's in progress.
    return error_number == WSAEWOULDBLOCK || error_number == WSAEINPROGRESS;
}


HakoPduErrorType close_socket(SocketHandle fd) noexcept
{
    if (!is_valid_socket(fd)) {
        return HAKO_PDU_ERR_OK;
    }
    return (::closesocket(fd) == 0) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

void shutdown_socket(SocketHandle fd, SocketShutdownMode mode) noexcept
{
    if (!is_valid_socket(fd)) {
        return;
    }
    int how;
    switch (mode) {
        case SocketShutdownMode::Read:
            how = SD_RECEIVE;
            break;
        case SocketShutdownMode::ReadWrite:
        default:
            how = SD_BOTH;
            break;
    }
    (void)::shutdown(fd, how);
}

HakoPduErrorType set_socket_nonblocking(SocketHandle fd, bool enabled) noexcept
{
    u_long mode = enabled ? 1 : 0;
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}

SocketHandle create_socket(int family, int type, int protocol) noexcept
{
    return ::socket(family, type, protocol);
}

SocketHandle accept_socket(SocketHandle fd, SocketAddress* address, SocketLength* address_len) noexcept
{
    return ::accept(fd, address, address_len);
}

int connect_socket(SocketHandle fd, const SocketAddress* address, SocketLength address_len) noexcept
{
    return ::connect(fd, address, address_len);
}

int bind_socket(SocketHandle fd, const SocketAddress* address, SocketLength address_len) noexcept
{
    return ::bind(fd, address, address_len);
}

int listen_socket(SocketHandle fd, int backlog) noexcept
{
    return ::listen(fd, backlog);
}

SocketSize recv_socket(SocketHandle fd, std::byte* buffer, size_t size, int flags) noexcept
{
    return ::recv(fd, reinterpret_cast<char*>(buffer), static_cast<int>(size), flags);
}

SocketSize send_socket(SocketHandle fd, const std::byte* buffer, size_t size, int flags) noexcept
{
    return ::send(fd, reinterpret_cast<const char*>(buffer), static_cast<int>(size), flags);
}

SocketSize recv_from_socket(SocketHandle fd,
                            std::byte* buffer,
                            size_t size,
                            int flags,
                            SocketAddress* address,
                            SocketLength* address_len) noexcept
{
    return ::recvfrom(fd, reinterpret_cast<char*>(buffer), static_cast<int>(size), flags, address, address_len);
}

SocketSize send_to_socket(SocketHandle fd,
                          const std::byte* buffer,
                          size_t size,
                          int flags,
                          const SocketAddress* address,
                          SocketLength address_len) noexcept
{
    return ::sendto(fd, reinterpret_cast<const char*>(buffer), static_cast<int>(size), flags, address, address_len);
}

HakoPduErrorType set_socket_option_int(SocketHandle fd, int level, int optname, int value) noexcept
{
    return (::setsockopt(fd, level, optname, reinterpret_cast<const char*>(&value), sizeof(value)) == 0)
        ? HAKO_PDU_ERR_OK
        : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType get_socket_option_int(SocketHandle fd, int level, int optname, int& value) noexcept
{
    SocketLength value_len = sizeof(value);
    return (::getsockopt(fd, level, optname, reinterpret_cast<char*>(&value), &value_len) == 0)
        ? HAKO_PDU_ERR_OK
        : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType set_socket_option_buffer(SocketHandle fd,
                                          int level,
                                          int optname,
                                          const void* value,
                                          SocketLength value_len) noexcept
{
    return (::setsockopt(fd, level, optname, static_cast<const char*>(value), value_len) == 0)
        ? HAKO_PDU_ERR_OK
        : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType set_socket_timeout_option(SocketHandle fd, int optname, int timeout_ms) noexcept
{
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    return set_socket_option_buffer(fd, SOL_SOCKET, optname, &timeout, sizeof(timeout));
}

HakoPduErrorType set_socket_linger_option(SocketHandle fd, bool enabled, int timeout_sec) noexcept
{
    linger linger_opts{};
    linger_opts.l_onoff = enabled ? 1 : 0;
    linger_opts.l_linger = static_cast<u_short>(timeout_sec);
    return set_socket_option_buffer(fd, SOL_SOCKET, SO_LINGER, &linger_opts, sizeof(linger_opts));
}

void free_address_info(AddressInfo* info) noexcept
{
    if (info != nullptr) {
        ::freeaddrinfo(info);
    }
}

HakoPduErrorType wait_socket(SocketHandle fd,
                             SocketWaitCondition condition,
                             int timeout_ms,
                             bool& ready) noexcept
{
    ready = false;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int result;
    if (condition == SocketWaitCondition::Writable) {
        result = ::select(0, nullptr, &fds, nullptr, &timeout);
    } else {
        result = ::select(0, &fds, nullptr, nullptr, &timeout);
    }

    if (result == 0) {
        return HAKO_PDU_ERR_TIMEOUT;
    }
    if (result == SOCKET_ERROR) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    ready = (result > 0);
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa

#endif // _WIN32
