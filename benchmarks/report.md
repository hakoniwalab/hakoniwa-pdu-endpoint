# Hakoniwa PDU Endpoint Benchmark Report

## Measurement Environment

- OS: macOS on Apple Silicon M2
- Memory: 32 GB
- Benchmark target:
  - TCP
  - SHM(callback)
- Method:
  - Use Hakoniwa PDU endpoint benchmark runners.
  - Switch the endpoint backend by JSON configuration.
  - Use the same runner logic, PDU definition, send pattern, and benchmark analyzer for both backends.

## Workload

- Data type: camera data
- PDU name: `camera_data`
- PDU size: 1,002,992 bytes
- Logical channel ID: 16
- Sent PDU count: 1024
- Sent key pattern: `Drone-1` through `Drone-1024`
- Total payload size:
  - 1,027,063,808 bytes
  - Approximately 1.027 GB
  - Approximately 979.5 MiB

This benchmark measures batch-level effective performance through the Hakoniwa PDU endpoint layer. It is not a bare TCP or bare shared-memory microbenchmark. The measured path includes endpoint send/receive behavior, PDU key handling, transport framing or shared-memory access, callback dispatch, timestamp capture, and in-memory benchmark log buffering.

## Results

| Metric | TCP | SHM(callback) |
| --- | ---: | ---: |
| sent | 1024 | 1024 |
| received | 1024 | 1024 |
| loss_count | 0 | 0 |
| completed | 1 | 1 |
| send_duration_ms | 1662.130 ms | 78.419 ms |
| recv_span_ms | 1655.000 ms | 241.696 ms |
| end_to_end_ms | 1662.292 ms | 243.370 ms |
| tail_after_send_ms | 0.160 ms | 164.951 ms |

## Throughput Comparison

| Viewpoint | TCP | SHM(callback) | Comparison |
| --- | ---: | ---: | ---: |
| End-to-end throughput | about 618 MB/s | about 4.22 GB/s | SHM is about 6.8x faster |
| Publisher send throughput | about 618 MB/s | about 13.1 GB/s | SHM is about 21.2x faster |

## Metric Notes

- `send_duration_ms`: Time from the publisher starting the first send until it finishes the final send.
- `recv_span_ms`: Time from the subscriber's first receive callback until its final receive callback.
- `end_to_end_ms`: Time from the publisher starting the batch until the subscriber receives the final PDU.
- `tail_after_send_ms`: Time from publisher send completion until subscriber final receive completion.

`end_to_end_ms` is the main whole-batch completion metric. `send_duration_ms` is useful for isolating publisher-side send/write path cost. `tail_after_send_ms` shows how much subscriber-side work remains after the publisher has already finished sending.

## Analysis

TCP shows almost the same `send_duration_ms` and `recv_span_ms`, and its `tail_after_send_ms` is very small at 0.160 ms.

This indicates that TCP backpressure keeps the publisher and subscriber moving almost synchronously. The publisher is naturally pulled by the subscriber's receive progress, so there is little remaining receive backlog after the publisher finishes sending.

SHM(callback), on the other hand, has a much shorter publisher-side `send_duration_ms` of 78.419 ms. It writes about 1 GB of camera payload very quickly.

However, its `tail_after_send_ms` is 164.951 ms. This means that after the publisher finishes writing, subscriber-side callback dispatch and receive processing still continue for a substantial amount of time.

In short, SHM(callback) has a very strong write/send path, but for large payload batches the subscriber callback side becomes visible as tail latency.

## Summary

For this camera data benchmark with 1024 PDUs and about 1.027 GB of total payload:

- Both TCP and SHM(callback) successfully sent and received all 1024 PDUs.
- Both backends had `loss_count=0`.
- SHM(callback) was about 6.8x faster than TCP in end-to-end batch completion.
- SHM(callback) was about 21.2x faster than TCP on the publisher send/write path.
- TCP progressed in a synchronized pub/sub pattern due to backpressure.
- SHM(callback) let the publisher run far ahead, leaving subscriber callback processing visible in `tail_after_send_ms`.

The result is not only that SHM(callback) is faster overall, but also that TCP and SHM(callback) expose different execution models clearly through `send_duration_ms`, `recv_span_ms`, and `tail_after_send_ms`.
