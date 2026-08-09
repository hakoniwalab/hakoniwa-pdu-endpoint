#include "hakoniwa/pdu/socket_portability.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace hakoniwa::pdu;

#ifdef _WIN32

TEST(SocketPortabilityTest, WindowsTimeoutIsNotRetryableWouldBlock)
{
    EXPECT_TRUE(is_socket_would_block(WSAEWOULDBLOCK));
    EXPECT_FALSE(is_socket_would_block(WSAETIMEDOUT));
    EXPECT_TRUE(is_socket_timeout(WSAETIMEDOUT));
    EXPECT_EQ(map_errno_to_error(WSAETIMEDOUT), HAKO_PDU_ERR_TIMEOUT);
    EXPECT_FALSE(is_socket_would_block(WSAECONNRESET));
}

#else

TEST(SocketPortabilityTest, PosixWouldBlockRemainsRetryable)
{
    EXPECT_TRUE(is_socket_would_block(EAGAIN));
    EXPECT_FALSE(is_socket_timeout(EAGAIN));
}

TEST(SocketPortabilityTest, CreatedSocketDisablesSigpipeWhenSupported)
{
#if defined(SO_NOSIGPIPE)
    const SocketHandle fd = create_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(is_valid_socket(fd));

    int enabled = 0;
    ASSERT_EQ(get_socket_option_int(fd, SOL_SOCKET, SO_NOSIGPIPE, enabled), HAKO_PDU_ERR_OK);
    EXPECT_EQ(enabled, 1);

    EXPECT_EQ(close_socket(fd), HAKO_PDU_ERR_OK);
#else
    GTEST_SKIP() << "SO_NOSIGPIPE is not available on this platform";
#endif
}

TEST(SocketPortabilityTest, SendReturnsErrorInsteadOfRaisingSigpipe)
{
#if defined(MSG_NOSIGNAL)
    int fds[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_EQ(::close(fds[1]), 0);

    const std::byte payload{0x01};
    errno = 0;
    const SocketSize sent = send_socket(fds[0], &payload, 1, 0);

    EXPECT_EQ(sent, static_cast<SocketSize>(-1));
    EXPECT_EQ(errno, EPIPE);
    EXPECT_EQ(close_socket(fds[0]), HAKO_PDU_ERR_OK);
#else
    GTEST_SKIP() << "MSG_NOSIGNAL is not available on this platform";
#endif
}

#endif
