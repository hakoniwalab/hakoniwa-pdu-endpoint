#include "hakoniwa/pdu/socket_portability.hpp"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace hakoniwa {
namespace pdu {

bool is_valid_socket(SocketHandle fd) noexcept
{
    return fd >= 0;
}

int get_socket_status_flags(SocketHandle fd) noexcept
{
    return fcntl(fd, F_GETFL, 0);
}

HakoPduErrorType set_socket_status_flags(SocketHandle fd, int flags) noexcept
{
    return (fcntl(fd, F_SETFL, flags) == 0) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

int last_socket_error() noexcept
{
    return errno;
}

std::string socket_error_message(int error_number)
{
    return std::strerror(error_number);
}

bool is_socket_would_block(int error_number) noexcept
{
    return error_number == EAGAIN || error_number == EWOULDBLOCK;
}

bool is_socket_timeout(int error_number) noexcept
{
    (void)error_number;
    return false;
}

bool is_socket_interrupted(int error_number) noexcept
{
    return error_number == EINTR;
}

bool is_socket_connect_in_progress(int error_number) noexcept
{
    return error_number == EINPROGRESS;
}

HakoPduErrorType close_socket(SocketHandle fd) noexcept
{
    if (!is_valid_socket(fd)) {
        return HAKO_PDU_ERR_OK;
    }
    return (::close(fd) == 0) ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

void shutdown_socket(SocketHandle fd, SocketShutdownMode mode) noexcept
{
    if (!is_valid_socket(fd)) {
        return;
    }
    const int how = (mode == SocketShutdownMode::Read) ? SHUT_RD : SHUT_RDWR;
    (void)::shutdown(fd, how);
}

HakoPduErrorType set_socket_nonblocking(SocketHandle fd, bool enabled) noexcept
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    const int next_flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(fd, F_SETFL, next_flags) != 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    return HAKO_PDU_ERR_OK;
}

SocketHandle create_socket(int family, int type, int protocol) noexcept
{
    SocketHandle fd = ::socket(family, type, protocol);
#if defined(SO_NOSIGPIPE)
    if (is_valid_socket(fd)) {
        const int enabled = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
    }
#endif
    return fd;
}

SocketHandle accept_socket(SocketHandle fd, SocketAddress* address, SocketLength* address_len) noexcept
{
    SocketHandle accepted_fd = ::accept(fd, address, address_len);
#if defined(SO_NOSIGPIPE)
    if (is_valid_socket(accepted_fd)) {
        const int enabled = 1;
        (void)::setsockopt(accepted_fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
    }
#endif
    return accepted_fd;
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
    return ::recv(fd, buffer, size, flags);
}

SocketSize send_socket(SocketHandle fd, const std::byte* buffer, size_t size, int flags) noexcept
{
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;
#endif
    return ::send(fd, buffer, size, flags);
}

SocketSize recv_from_socket(SocketHandle fd,
                            std::byte* buffer,
                            size_t size,
                            int flags,
                            SocketAddress* address,
                            SocketLength* address_len) noexcept
{
    return ::recvfrom(fd, buffer, size, flags, address, address_len);
}

SocketSize send_to_socket(SocketHandle fd,
                          const std::byte* buffer,
                          size_t size,
                          int flags,
                          const SocketAddress* address,
                          SocketLength address_len) noexcept
{
    return ::sendto(fd, buffer, size, flags, address, address_len);
}

HakoPduErrorType set_socket_option_int(SocketHandle fd, int level, int optname, int value) noexcept
{
    return (::setsockopt(fd, level, optname, &value, sizeof(value)) == 0)
        ? HAKO_PDU_ERR_OK
        : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType get_socket_option_int(SocketHandle fd, int level, int optname, int& value) noexcept
{
    SocketLength value_len = sizeof(value);
    return (::getsockopt(fd, level, optname, &value, &value_len) == 0)
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
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return set_socket_option_buffer(fd, SOL_SOCKET, optname, &timeout, sizeof(timeout));
}

HakoPduErrorType set_socket_linger_option(SocketHandle fd, bool enabled, int timeout_sec) noexcept
{
    linger linger_opts{};
    linger_opts.l_onoff = enabled ? 1 : 0;
    linger_opts.l_linger = timeout_sec;
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
    pollfd poll_fd{};
    poll_fd.fd = fd;
    poll_fd.events = (condition == SocketWaitCondition::Writable) ? POLLOUT : POLLIN;

    const int result = ::poll(&poll_fd, 1, timeout_ms);
    if (result == 0) {
        return HAKO_PDU_ERR_TIMEOUT;
    }
    if (result < 0) {
        return HAKO_PDU_ERR_TIMEOUT;
    }
    ready = true;
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa

#endif
