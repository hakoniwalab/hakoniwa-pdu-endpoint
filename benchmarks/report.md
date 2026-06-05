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
- Callback benchmark setting:
  - `recv_cache_write=false`
  - The subscriber receives data through callbacks and skips Endpoint cache writes.
  - This setting is applied to both TCP and SHM(callback).
- Result files:
  - `benchmarks/results/summary.csv`
  - `benchmarks/results/summary.json`

This benchmark measures batch-level effective performance through the Hakoniwa PDU endpoint layer. It is not a bare TCP or bare shared-memory microbenchmark. The measured path includes endpoint send/receive behavior, PDU key handling, transport framing or shared-memory access, callback dispatch, timestamp capture, and in-memory benchmark log buffering.

## Workload

All measurements use 1024 PDU sends.

| PDU name | Type | Channel ID | PDU size | Total payload |
| --- | --- | ---: | ---: | ---: |
| `disturb` | `hako_msgs/Disturbance` | 3 | 256 bytes | 262,144 bytes |
| `lidar_points` | `sensor_msgs/PointCloud2` | 16 | 177,424 bytes | 181,682,176 bytes |
| `hako_camera_data` | `hako_msgs/HakoCameraData` | 12 | 1,002,992 bytes | 1,027,063,808 bytes |

The send key pattern is `Drone-1/<pdu_name>` through `Drone-1024/<pdu_name>`.

## Correctness

All benchmark runs completed without loss.

| PDU name | Protocol | sent | received | completed | loss_count |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 1024 | 1024 | 1 | 0 |
| `disturb` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `lidar_points` | TCP | 1024 | 1024 | 1 | 0 |
| `lidar_points` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | TCP | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | SHM(callback) | 1024 | 1024 | 1 | 0 |

## Timing Results

| PDU name | Protocol | send_duration_ms | recv_span_ms | end_to_end_ms | tail_after_send_ms |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 10.031 | 9.919 | 10.083 | 0.051 |
| `disturb` | SHM(callback) | 5.231 | 2.211 | 6.145 | 0.914 |
| `lidar_points` | TCP | 302.128 | 300.498 | 302.152 | 0.023 |
| `lidar_points` | SHM(callback) | 21.476 | 23.373 | 29.456 | 7.980 |
| `hako_camera_data` | TCP | 1576.270 | 1566.730 | 1576.351 | 0.078 |
| `hako_camera_data` | SHM(callback) | 94.678 | 130.965 | 132.303 | 37.625 |

## Throughput Results

Throughput uses decimal MB/s.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 26.1 MB/s | 26.0 MB/s |
| `disturb` | SHM(callback) | 50.1 MB/s | 42.7 MB/s |
| `lidar_points` | TCP | 601.3 MB/s | 601.3 MB/s |
| `lidar_points` | SHM(callback) | 8.46 GB/s | 6.17 GB/s |
| `hako_camera_data` | TCP | 651.6 MB/s | 651.5 MB/s |
| `hako_camera_data` | SHM(callback) | 10.85 GB/s | 7.76 GB/s |

## Relative Performance

| PDU name | End-to-end speedup | Publisher send speedup |
| --- | ---: | ---: |
| `disturb` | SHM is 1.64x faster | SHM is 1.92x faster |
| `lidar_points` | SHM is 10.26x faster | SHM is 14.07x faster |
| `hako_camera_data` | SHM is 11.91x faster | SHM is 16.65x faster |

## Metric Notes

- `send_duration_ms`: Time from the publisher starting the first send until it finishes the final send.
- `recv_span_ms`: Time from the subscriber's first receive callback until its final receive callback.
- `end_to_end_ms`: Time from the publisher starting the batch until the subscriber receives the final PDU.
- `tail_after_send_ms`: Time from publisher send completion until subscriber final receive completion.

`end_to_end_ms` is the main whole-batch completion metric. `send_duration_ms` isolates publisher-side send/write path cost. `tail_after_send_ms` shows how much subscriber-side work remains after the publisher has already finished sending.

## Analysis

TCP shows a consistent pattern across all PDU sizes: `send_duration_ms` and `recv_span_ms` are nearly the same, and `tail_after_send_ms` is very small.

This indicates that TCP backpressure keeps the publisher and subscriber moving almost synchronously. The publisher is naturally pulled by the subscriber's receive progress, so little receive backlog remains after the publisher finishes sending.

SHM(callback) shows a different pattern. The publisher send path is much faster, especially for large PDU payloads. For `hako_camera_data`, SHM(callback) sends about 1.027 GB in 94.678 ms, while TCP takes 1576.270 ms.

The subscriber-side tail is now much smaller than the previous cache-writing SHM(callback) result. With `recv_cache_write=false`, the subscriber avoids writing each received PDU into the Endpoint cache and delivers the data directly through the callback path. For `hako_camera_data`, SHM(callback) `tail_after_send_ms` is 37.625 ms and end-to-end completion is 132.303 ms.

The same callback-only setting also improves TCP. For `hako_camera_data`, TCP end-to-end completion is 1576.351 ms in this final result. TCP remains dominated by socket streaming and backpressure, so the improvement is smaller than the SHM receive-cache-write reduction.

This confirms that the Endpoint receive-cache write path was a major cost for large callback-style SHM batches. SHM(callback) still allows the publisher to run ahead of the subscriber, but the remaining tail is now mostly callback dispatch and receive processing rather than an additional cache copy/write path.

## Summary

- TCP and SHM(callback) both delivered all tested PDUs without loss.
- SHM(callback) outperformed TCP end-to-end for all tested PDU sizes.
- The SHM(callback) advantage becomes much larger as PDU size grows.
- For `hako_camera_data`, end-to-end throughput improved to about 7.76 GB/s.
- For `hako_camera_data`, SHM(callback) is about 11.91x faster than TCP end-to-end.
- For `hako_camera_data`, the publisher send/write path is about 16.65x faster than TCP.
- TCP behaves as a synchronized streaming pipeline due to backpressure.
- SHM(callback) gives much higher local IPC throughput under the callback-only receive condition.

## Ubuntu Measurement Addendum

Additional measurements were collected on Ubuntu on June 5, 2026 for the same `hako_camera_data` workload with 1024 sends.

### Environment

- OS: Ubuntu Core 24
- Kernel: Linux 5.15.167.4-microsoft-standard-WSL2
- Runtime: WSL2 on Microsoft Hyper-V
- CPU: Intel(R) Core(TM) Ultra 7 155H
- CPU topology: 1 socket, 11 cores, 22 threads
- Cache:
  - L1d: 528 KiB
  - L1i: 704 KiB
  - L2: 22 MiB
  - L3: 24 MiB
- Memory: 31 GiB RAM
- Swap: 8.0 GiB
- PDU name: `hako_camera_data`
- PDU size: 1,002,992 bytes
- Total payload: 1,027,063,808 bytes
- Protocols:
  - TCP
  - SHM(callback)

### Correctness

Both Ubuntu runs completed without loss.

| PDU name | Protocol | sent | received | completed | loss_count |
| --- | --- | ---: | ---: | ---: | ---: |
| `hako_camera_data` | TCP | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | SHM(callback) | 1024 | 1024 | 1 | 0 |

### Timing Results

| PDU name | Protocol | send_duration_ms | recv_span_ms | end_to_end_ms | tail_after_send_ms |
| --- | --- | ---: | ---: | ---: | ---: |
| `hako_camera_data` | TCP | 3937.750 | 3933.420 | 3938.326 | 0.574 |
| `hako_camera_data` | SHM(callback) | 344.898 | 344.104 | 344.973 | 0.075 |

### Throughput Results

Throughput uses decimal MB/s.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `hako_camera_data` | TCP | 260.8 MB/s | 260.8 MB/s |
| `hako_camera_data` | SHM(callback) | 2.98 GB/s | 2.98 GB/s |

### Relative Performance

| PDU name | End-to-end speedup | Publisher send speedup |
| --- | ---: | ---: |
| `hako_camera_data` | SHM is 11.42x faster | SHM is 11.42x faster |

### Ubuntu Notes

The Ubuntu result preserves the same qualitative behavior as the macOS measurement: TCP send and receive progress stay tightly coupled, while SHM(callback) completes the same batch much faster.

The absolute performance in this Ubuntu run is lower than the macOS M2 result for both protocols. For `hako_camera_data`, TCP end-to-end completion increased from 1576.351 ms on macOS to 3938.326 ms on Ubuntu, and SHM(callback) increased from 132.303 ms to 344.973 ms.
