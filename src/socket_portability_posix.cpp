#include "hakoniwa/pdu/socket_portability.hpp"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hakoniwa {
namespace pdu {

bool is_valid_socket(SocketHandle fd) noexcept
{
    return fd >= 0;
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
        return is_socket_interrupted(last_socket_error()) ? HAKO_PDU_ERR_TIMEOUT : HAKO_PDU_ERR_IO_ERROR;
    }
    ready = true;
    return HAKO_PDU_ERR_OK;
}

}  // namespace pdu
}  // namespace hakoniwa

#endif
