# Hakoniwa PDU Endpoint Benchmarks

This directory contains benchmark runners and configuration files for measuring the effective communication performance of the Hakoniwa PDU endpoint layer.

The benchmark is not intended to measure a bare TCP, UDP, or shared-memory implementation in isolation. It measures the end-to-end cost of sending and receiving Hakoniwa PDU payloads through the endpoint abstraction under controlled benchmark conditions.

In other words, this benchmark is for answering questions such as:

- How long does it take to send N Hakoniwa PDUs through a selected endpoint backend?
- How long does it take until the subscriber has received all N PDUs?
- Does the selected backend lose messages under the current benchmark conditions?
- How much of the batch completion time appears on the publisher side, subscriber side, or after publisher completion?

## Benchmark positioning

The benchmark keeps the application-level payload format stable and changes the endpoint backend through the benchmark configuration.

The intended comparison target is:

```text
same PDU type
same PDU binary conversion
same robot/key expansion pattern
same benchmark runner logic
same endpoint abstraction
 different endpoint backend: shm / tcp / udp
```

This means the result should be interpreted as the effective performance of Hakoniwa PDU endpoint communication, not as a theoretical maximum of the underlying transport protocol.

For example, a TCP result includes not only socket write/read behavior, but also the endpoint send path, PDU key resolution, packet framing, callback dispatch, benchmark timestamp capture, and in-memory benchmark log buffering.

## Directory structure

```text
benchmarks/
  README.md
  configs/
    benchmark-tcp.json
    benchmark-udp.json
    endpoint/
      publisher/
        publisher_tcp.json
        publisher_udp.json
        ...
      subscriber/
        subscriber_tcp.json
        subscriber_udp.json
        ...
      cache/
        buffer.json
    pdu/
      pdudef.json
  logs/
    benchmark-tcp_pub.log
    benchmark-tcp_sub.log
    benchmark-udp_pub.log
    benchmark-udp_sub.log
  runner/
    benchmark_pub_runner.cpp
    benchmark_sub_runner.cpp
    comm/
      runner.hpp
      pub_runner.hpp
      sub_runner.hpp
      pub_runner_tcp.cpp
      sub_runner_tcp.cpp
      pub_runner_udp.cpp
      sub_runner_udp.cpp
      pub_runner_shm.cpp
      sub_runner_shm.cpp
  tools/
    measure_latency.py
```

## Source layout

### Runner entry points

The benchmark executable entry points are:

```text
benchmarks/runner/benchmark_pub_runner.cpp
benchmarks/runner/benchmark_sub_runner.cpp
```

These programs load a benchmark config file, select the endpoint backend, and run the corresponding publisher or subscriber runner.

On builds without Hakoniwa core support, only the `tcp` and `udp` runners are available. In particular, the native Windows default build excludes the `shm` benchmark runner unless Hakoniwa core support is enabled for that build.

### Common runner logic

Common benchmark logic is in:

```text
benchmarks/runner/comm/runner.hpp
```

This file contains shared functionality such as:

- benchmark config loading
- PDU definition expansion for benchmark robot keys
- benchmark timestamp acquisition
- in-memory benchmark log buffering
- subscriber receive completion waiting
- receive-event timing helpers

### Backend-specific runners

Backend-specific publisher and subscriber behavior is implemented under:

```text
benchmarks/runner/comm/pub_runner_*.cpp
benchmarks/runner/comm/sub_runner_*.cpp
```

For TCP and UDP, the subscriber receives data through endpoint receive callbacks. The subscriber `run()` function does not poll the endpoint for data. Instead, it waits until the receive callback observes the expected number of PDUs or until timeout.

This is intentional: the benchmark measurement point for receive-side timing is the data receive event, not a polling loop in `run()`.

## Configuration files

Benchmark execution is controlled by files such as:

```text
benchmarks/configs/benchmark-tcp.json
benchmarks/configs/benchmark-udp.json
```

Example:

```json
{
    "protocol": "tcp",
    "try_num": 1024,
    "timeout_sec": 30,
    "config_path": "benchmarks/configs",
    "log_path": "benchmarks/logs/benchmark-tcp.log"
}
```

### Benchmark config fields

| Field | Meaning |
| --- | --- |
| `protocol` | Endpoint backend used by the benchmark. Current values include `tcp`, `udp`, and `shm` depending on runner support. |
| `try_num` | Number of benchmark PDU sends expected in one batch. The publisher sends this many PDUs, and the subscriber waits for this many receive events. |
| `timeout_sec` | Subscriber wait timeout in seconds. If all expected PDUs are not received before this timeout, the subscriber reports incomplete completion. |
| `config_path` | Base path for endpoint, cache, and PDU definition configuration files. |
| `log_path` | Base benchmark log path. The runner derives role-specific log files from this path. |

Given this example:

```json
"log_path": "benchmarks/logs/benchmark-tcp.log"
```

the runners write:

```text
benchmarks/logs/benchmark-tcp_pub.log
benchmarks/logs/benchmark-tcp_sub.log
```

### Endpoint config files

Endpoint-specific settings are kept under:

```text
benchmarks/configs/endpoint/
```

The publisher and subscriber runners select endpoint config files based on the benchmark protocol. For example:

```text
benchmarks/configs/endpoint/publisher/publisher_tcp.json
benchmarks/configs/endpoint/subscriber/subscriber_tcp.json
benchmarks/configs/endpoint/publisher/publisher_udp.json
benchmarks/configs/endpoint/subscriber/subscriber_udp.json
```

These files describe the endpoint-side communication settings, cache settings, and PDU definition path used by each runner.

### PDU definition config

The base PDU definition is stored under:

```text
benchmarks/configs/pdu/pdudef.json
```

The benchmark uses `Drone-1/pos` as a template PDU key and expands it in memory for `Drone-2/pos`, `Drone-3/pos`, and so on, up to `try_num`.

The expanded keys represent robot-specific PDU namespaces. The logical channel id may be the same for different robots because the effective key is the robot namespace plus the logical channel.

## What this benchmark measures

The current latency tool measures batch-level timing.

It does not measure one-message round-trip latency. It measures how long it takes to send a batch of N PDUs and how long it takes until the subscriber receives the batch.

The main timestamps are:

```text
send_start_ns   publisher timestamp just before sending the first PDU
send_end_ns     publisher timestamp just after sending the final PDU
first_recv_ns   subscriber timestamp at the first receive callback
last_recv_ns    subscriber timestamp at the final receive callback
```

From these timestamps, the analyzer computes batch-level timing metrics.

## Running a benchmark

Build the project first, then start the subscriber before the publisher.

For TCP:

```bash
./build/benchmarks/runner/benchmark_sub_runner benchmarks/configs/benchmark-tcp.json
./build/benchmarks/runner/benchmark_pub_runner benchmarks/configs/benchmark-tcp.json
```

For UDP:

```bash
./build/benchmarks/runner/benchmark_sub_runner benchmarks/configs/benchmark-udp.json
./build/benchmarks/runner/benchmark_pub_runner benchmarks/configs/benchmark-udp.json
```

The runners write benchmark logs to the files derived from `log_path`.

For example, TCP writes:

```text
benchmarks/logs/benchmark-tcp_pub.log
benchmarks/logs/benchmark-tcp_sub.log
```

## Log output

The publisher log contains records such as:

```text
BENCH_PUB_EVENT protocol=tcp robot=Drone-1 pdu=pos size=72 count=1 send_ns=...
BENCH_PUB_SUMMARY protocol=tcp expected=1024 sent=1024 send_start_ns=... send_end_ns=... send_duration_ms=...
```

The subscriber log contains records such as:

```text
BENCH_SUB_WAIT protocol=tcp expected=1024 timeout_sec=30
BENCH_SUB_EVENT protocol=tcp robot=Drone-1 channel=1 size=72 count=1 recv_ns=...
BENCH_SUB_SUMMARY protocol=tcp expected=1024 received=1024 first_recv_ns=... last_recv_ns=... recv_span_ms=... completed=1
```

Benchmark log records are written in a simple key-value format so they can be parsed by scripts.

During measurement, benchmark records are buffered in memory. File writing is performed after the benchmark run completes, so benchmark file I/O is not part of the measured send/receive loop.

## Analyzing benchmark logs

Use:

```text
benchmarks/tools/measure_latency.py
```

Example:

```bash
python3 benchmarks/tools/measure_latency.py \
  --pub-log benchmarks/logs/benchmark-tcp_pub.log \
  --sub-log benchmarks/logs/benchmark-tcp_sub.log
```

The analyzer reads `BENCH_PUB_SUMMARY` and `BENCH_SUB_SUMMARY`, then computes batch-level timing metrics.

Use `--strict` to return a non-zero exit code when the send/receive counts do not match or the subscriber did not complete.

```bash
python3 benchmarks/tools/measure_latency.py \
  --pub-log benchmarks/logs/benchmark-tcp_pub.log \
  --sub-log benchmarks/logs/benchmark-tcp_sub.log \
  --strict
```

## Analyzer output fields

Example output:

```text
protocol=tcp
expected=1024
sent=1024
received=1024
completed=1
loss_count=0
loss_rate=0.000000
send_duration_ms=11.221200
recv_span_ms=11.152400
first_receive_latency_ms=0.095167
end_to_end_ms=11.247584
tail_after_send_ms=0.026334
```

### Field meanings

| Field | Meaning |
| --- | --- |
| `protocol` | Endpoint backend used in the benchmark. |
| `expected` | Number of PDUs expected by the benchmark. Usually equal to `try_num`. |
| `sent` | Number of PDUs successfully sent by the publisher. |
| `received` | Number of PDUs received by the subscriber. |
| `completed` | `1` if the subscriber received all expected PDUs before timeout. `0` otherwise. |
| `loss_count` | `sent - received`. For TCP this should normally be zero if the run completes. For UDP this may be non-zero. |
| `loss_rate` | `loss_count / sent`. |
| `send_duration_ms` | Time from `send_start_ns` to `send_end_ns`. This is the time spent by the publisher to issue all send API calls in the batch. |
| `recv_span_ms` | Time from `first_recv_ns` to `last_recv_ns`. This is the subscriber-side receive span for the batch. |
| `first_receive_latency_ms` | Time from publisher send start to the first subscriber receive callback. Formula: `first_recv_ns - send_start_ns`. |
| `end_to_end_ms` | Batch completion time from publisher send start to the final subscriber receive callback. Formula: `last_recv_ns - send_start_ns`. |
| `tail_after_send_ms` | Time from publisher send completion to final subscriber receive callback. Formula: `last_recv_ns - send_end_ns`. |

## How to interpret the metrics

### `end_to_end_ms` is batch completion time

`end_to_end_ms` is not per-message latency. It is the time from starting the batch send until the subscriber receives the final PDU in the batch.

For per-message latency, a different benchmark design is needed, such as sending one PDU and waiting for its corresponding receive event or acknowledgment.

### `send_duration_ms` shows publisher-side batch send cost

`send_duration_ms` is the time required for the publisher to issue all send API calls.

This includes the endpoint send path and benchmark instrumentation inside the send loop, such as timestamp capture and in-memory log buffering. It does not include benchmark file I/O.

If `send_duration_ms` is large, possible causes include:

- endpoint send path overhead
- PDU key lookup or resolved-key construction
- packet framing or buffer copying
- many small TCP writes or UDP sends
- transport-level backpressure
- benchmark instrumentation overhead

### `recv_span_ms` shows subscriber-side receive spread

`recv_span_ms` measures how long the subscriber takes from the first receive event to the final receive event.

If `recv_span_ms` is much larger than `send_duration_ms`, the subscriber side may be a bottleneck.

If `recv_span_ms` is close to `send_duration_ms`, the benchmark is likely behaving as a streaming pipeline: the publisher sends the batch gradually while the subscriber receives it almost in parallel.

### `tail_after_send_ms` helps identify receive backlog

`tail_after_send_ms` is useful for checking whether a backlog remains after the publisher finishes sending.

- Small `tail_after_send_ms`: the subscriber receives the final PDU almost immediately after the publisher finishes sending. This suggests the subscriber is keeping up with the publisher.
- Large `tail_after_send_ms`: the publisher finishes sending before the subscriber catches up. This suggests receive-side backlog or delayed callback processing.

### UDP interpretation

UDP does not guarantee delivery. For UDP benchmarks, always check:

```text
loss_count
loss_rate
completed
```

A timeout or non-zero loss count does not necessarily mean the endpoint API is broken. It may indicate UDP packet loss, socket buffer overflow, burst send pressure, or receive-side processing delay.

### TCP interpretation

TCP should normally deliver all sent data if the connection remains healthy. Therefore, TCP benchmarks are useful for separating endpoint overhead and streaming behavior from UDP loss behavior.

However, TCP send calls may still be affected by socket buffer capacity, receiver progress, and TCP backpressure. A TCP benchmark result should not be interpreted as a raw memory-copy benchmark.

## Example interpretation

Example:

```text
protocol=tcp
expected=1024
sent=1024
received=1024
completed=1
loss_count=0
loss_rate=0.000000
send_duration_ms=11.221200
recv_span_ms=11.152400
first_receive_latency_ms=0.095167
end_to_end_ms=11.247584
tail_after_send_ms=0.026334
```

This means:

- The publisher sent 1024 PDUs.
- The subscriber received all 1024 PDUs.
- No loss occurred.
- The publisher took about 11.22 ms to issue all send calls.
- The subscriber receive span was about 11.15 ms.
- The full batch completion time was about 11.25 ms.
- The first receive event occurred about 0.095 ms after publisher send start.
- The subscriber received the final PDU about 0.026 ms after the publisher finished sending.

Because `tail_after_send_ms` is very small and `send_duration_ms` is close to `recv_span_ms`, this result suggests that the system is behaving like a streaming pipeline. The subscriber is not significantly lagging after the publisher finishes. The batch completion time is dominated by how long it takes the publisher and endpoint path to stream all PDUs through the transport.

## Limitations

- The current tool measures batch-level timing, not per-message latency.
- Publisher and subscriber timestamps are taken in separate processes. On the same host, `std::chrono::steady_clock` is typically suitable for this benchmark, but the values should still be interpreted as host-local benchmark timing rather than distributed clock synchronization.
- Benchmark log records are buffered in memory during measurement and written after completion. This avoids file I/O in the measured send/receive path, but timestamp capture, string construction, and memory buffering are still part of the current instrumentation overhead.
- Results can vary depending on OS socket buffers, scheduler behavior, CPU load, endpoint configuration, and transport backend.
