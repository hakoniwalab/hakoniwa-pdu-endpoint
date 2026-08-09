# TCP Runtime Contract

[English](tcp-runtime-contract.md) | [日本語](tcp-runtime-contract-ja.md)

This document defines the runtime contract for the `tcp` and `tcp_mux`
communication providers.

## Timeout options

`options.read_timeout_ms` and `options.write_timeout_ms` are non-negative
milliseconds.

| Value | Meaning |
|---:|---|
| `0` | No blocking I/O timeout. This is the default and the recommended setting for persistent sessions. |
| Positive | A blocking read or write that reaches the deadline fails with `HAKO_PDU_ERR_TIMEOUT`. The connection is closed and must not be reused. |

The timeout is a connection error boundary. It is not a periodic wake-up used
to check whether the Endpoint should stop. `stop()` interrupts an indefinite
blocking receive by calling `shutdown` and closing the socket before joining
the receive thread.

This distinction matters on Windows. Winsock documents that a connection is in
an indeterminate state after a blocking `SO_RCVTIMEO` or `SO_SNDTIMEO`
expiration and should be closed. Consequently, `WSAETIMEDOUT` is never treated
as a retryable `WSAEWOULDBLOCK` condition.

Reference:

- [Microsoft Learn: SOL_SOCKET Socket Options](https://learn.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options)

On POSIX, blocking socket timeout expiration is commonly reported as
`EAGAIN`/`EWOULDBLOCK`. When blocking mode and a positive timeout are configured,
Endpoint maps that result to the same terminal `HAKO_PDU_ERR_TIMEOUT` contract.
A genuine non-blocking would-block condition remains retryable.

## Disconnect and reconnect ownership

The following events terminate the current TCP connection:

- peer EOF
- read or write timeout
- connection reset or another native socket error
- invalid packet framing
- explicit Endpoint stop

The normal `tcp` provider behaves as follows while it remains started:

- a server closes the failed connection and returns to `accept`
- a client closes the failed connection and returns to its connect loop

Each successful accept/connect receives a new connection ID. A failed socket is
never reused by a later connection.

For `tcp_mux`, the failed session is terminal. The listener continues accepting
new sessions, while the upper layer must replace the failed per-client Endpoint
with a newly accepted session.

`stop()` is an expected lifecycle transition. Socket errors caused by its
`shutdown + close` sequence do not emit an abnormal-disconnect notification.

## Diagnostics

TCP failures are recorded at the operation that observed the native result,
before it is mapped to `HakoPduErrorType`. Diagnostics include:

- monotonic timestamp in milliseconds
- transport (`tcp` or `tcp_mux`)
- communication configuration name
- role
- connection ID
- numeric peer address and port when available
- operation (`accept`, `recv_header`, `recv_body`, `send_packet`, and so on)
- native socket error code and OS message
- mapped Endpoint error
- configured timeout

Peer EOF, native socket errors, and packet framing errors are reported as
different events. For example:

```text
TCP transport error timestamp_msec=... transport=tcp comm=my-client role=client connection_id=4 peer=127.0.0.1:50051 operation=recv_header native_error=10060 native_message="A connection attempt failed ..." mapped_error=TIMEOUT timeout_ms=1000
```

The monotonic timestamp is intended for ordering events within one process. It
is not a wall-clock timestamp and must not be compared directly across hosts.

## Operational recommendation

Use `read_timeout_ms: 0` and `write_timeout_ms: 0` for long-lived simulation
connections unless an inactivity deadline is an explicit application
requirement. If a positive timeout is selected, the caller must expect the
current connection to end when it expires.
