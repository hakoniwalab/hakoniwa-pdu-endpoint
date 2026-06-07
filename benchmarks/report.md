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

Hakoniwa is an open-source simulation platform for cyber-physical systems, and this benchmark measures batch-level effective performance through its PDU endpoint layer. It is not a bare TCP or bare shared-memory microbenchmark. The measured path includes endpoint send/receive behavior, PDU key handling, transport framing or shared-memory access, callback dispatch, timestamp capture, and in-memory benchmark log buffering.

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

Throughput uses decimal units. Some rows are shown in MB/s and others in GB/s for readability.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 26.1 MB/s | 26.0 MB/s |
| `disturb` | SHM(callback) | 50.1 MB/s | 42.7 MB/s |
| `lidar_points` | TCP | 601.3 MB/s | 601.3 MB/s |
| `lidar_points` | SHM(callback) | 8.46 GB/s | 6.17 GB/s |
| `hako_camera_data` | TCP | 651.6 MB/s | 651.5 MB/s |
| `hako_camera_data` | SHM(callback) | 10.85 GB/s | 7.76 GB/s |

## Relative Performance

`SHM/TCP speedup` is defined as `TCP time / SHM time`. Values above `1.0` favor SHM(callback); values below `1.0` favor TCP.

| PDU name | SHM/TCP end-to-end speedup | SHM/TCP publisher send speedup |
| --- | ---: | ---: |
| `disturb` | 1.64x | 1.92x |
| `lidar_points` | 10.26x | 14.07x |
| `hako_camera_data` | 11.91x | 16.65x |

## Metric Notes

- `send_duration_ms`: Time from the publisher starting the first send until it finishes the final send.
- `recv_span_ms`: Time from the subscriber's first receive callback until its final receive callback.
- `end_to_end_ms`: Time from the publisher starting the batch until the subscriber receives the final PDU.
- `tail_after_send_ms`: Time from publisher send completion until subscriber final receive completion.

`end_to_end_ms` is the main whole-batch completion metric. `send_duration_ms` isolates publisher-side send/write path cost. `tail_after_send_ms` shows how much subscriber-side backlog or post-send processing remains after the publisher has already finished sending.

## Analysis

TCP shows a consistent pattern across all PDU sizes: `send_duration_ms` and `recv_span_ms` are nearly the same, and `tail_after_send_ms` is very small.

This indicates that TCP backpressure keeps the publisher and subscriber moving almost synchronously. The publisher is naturally pulled by the subscriber's receive progress, so little receive backlog remains after the publisher finishes sending.

SHM(callback) shows a different pattern. The publisher send path is much faster, especially for large PDU payloads. For `hako_camera_data`, SHM(callback) sends about 1.027 GB in 94.678 ms, while TCP takes 1576.270 ms.

With `recv_cache_write=false`, the subscriber avoids writing each received PDU into the Endpoint cache and delivers the data directly through the callback path. For `hako_camera_data`, SHM(callback) `tail_after_send_ms` is 37.625 ms and end-to-end completion is 132.303 ms.

The same callback-only setting is also applied to TCP. For `hako_camera_data`, TCP end-to-end completion is 1576.351 ms. TCP remains dominated by socket streaming and backpressure, so the benefit is smaller than on the SHM callback path.

This confirms that the Endpoint receive-cache write path was a major cost for large callback-style SHM batches. SHM(callback) still allows the publisher to run ahead of the subscriber, but the remaining tail is now mostly callback dispatch and receive processing rather than an additional cache copy/write path.

## Summary

- TCP and SHM(callback) both delivered all tested PDUs without loss in the macOS baseline measurements.
- In the macOS baseline measurements, SHM(callback) outperformed TCP end-to-end for all three tested PDU sizes.
- Across the published macOS and Ubuntu measurements, the SHM(callback) advantage becomes much larger as PDU size grows, although very small payloads can still favor TCP on Ubuntu.
- For `hako_camera_data`, end-to-end throughput improved to about 7.76 GB/s.
- For `hako_camera_data`, SHM(callback) is about 11.91x faster than TCP end-to-end.
- For `hako_camera_data`, the publisher send/write path is about 16.65x faster than TCP.
- TCP behaves as a synchronized streaming pipeline due to backpressure.
- SHM(callback) gives much higher local IPC throughput under the callback-only receive condition.

## Ubuntu Measurement Addendum

Additional measurements were collected on Ubuntu on June 5 and June 7, 2026 for the same three benchmark workloads with 1024 sends.

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
- PDU workloads:
  - `disturb`
  - `lidar_points`
  - `hako_camera_data`
- Protocols:
  - TCP
  - SHM(callback)
- Callback benchmark setting:
  - `recv_cache_write=false`
  - The same callback-only receive condition used in the macOS baseline was kept here for both TCP and SHM(callback).

### Correctness

All Ubuntu runs completed without loss.

| PDU name | Protocol | sent | received | completed | loss_count |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 1024 | 1024 | 1 | 0 |
| `disturb` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `lidar_points` | TCP | 1024 | 1024 | 1 | 0 |
| `lidar_points` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | TCP | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | SHM(callback) | 1024 | 1024 | 1 | 0 |

### Timing Results

| PDU name | Protocol | send_duration_ms | recv_span_ms | end_to_end_ms | tail_after_send_ms |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 2.485 | 2.422 | 2.487 | 0.003 |
| `disturb` | SHM(callback) | 5.753 | 4.777 | 9.273 | 3.520 |
| `lidar_points` | TCP | 499.363 | 498.540 | 499.422 | 0.059 |
| `lidar_points` | SHM(callback) | 57.220 | 56.938 | 57.230 | 0.010 |
| `hako_camera_data` | TCP | 3937.750 | 3933.420 | 3938.326 | 0.574 |
| `hako_camera_data` | SHM(callback) | 344.898 | 344.104 | 344.973 | 0.075 |

### Throughput Results

Throughput uses decimal units. Some rows are shown in MB/s and others in GB/s for readability.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 105.5 MB/s | 105.4 MB/s |
| `disturb` | SHM(callback) | 45.6 MB/s | 28.3 MB/s |
| `lidar_points` | TCP | 363.8 MB/s | 363.8 MB/s |
| `lidar_points` | SHM(callback) | 3.18 GB/s | 3.17 GB/s |
| `hako_camera_data` | TCP | 260.8 MB/s | 260.8 MB/s |
| `hako_camera_data` | SHM(callback) | 2.98 GB/s | 2.98 GB/s |

### Relative Performance

`SHM/TCP speedup` is defined as `TCP time / SHM time`. Values above `1.0` favor SHM(callback); values below `1.0` favor TCP.

| PDU name | SHM/TCP end-to-end speedup | SHM/TCP publisher send speedup |
| --- | ---: | ---: |
| `disturb` | 0.27x | 0.43x |
| `lidar_points` | 8.73x | 8.73x |
| `hako_camera_data` | 11.42x | 11.42x |

### Ubuntu Notes

The Ubuntu result preserves the same qualitative behavior as the macOS baseline and the native Windows execution validation for larger payloads: TCP send and receive progress stay tightly coupled, while SHM(callback) completes the same batch much faster for `lidar_points` and `hako_camera_data`.

For the smallest workload, `disturb`, this Ubuntu run shows the opposite result: TCP is faster than SHM(callback). In the current SHM(callback) path, fixed per-batch control costs such as callback dispatch, conductor-driven pacing, and the first-receive synchronization path are large relative to the 256-byte payload, so the shared-memory data path does not amortize those costs well in this case.

The absolute performance in this Ubuntu run is lower than the macOS M2 result for larger payloads. For `hako_camera_data`, TCP end-to-end completion increased from 1576.351 ms on macOS to 3938.326 ms on Ubuntu, and SHM(callback) increased from 132.303 ms to 344.973 ms.

## Native Windows Execution Validation

Native Windows build and end-to-end execution were validated on June 7, 2026 for the same three benchmark workloads with 1024 sends.

### Environment

- OS: native Windows
- Workload source:
  - `benchmarks/configs/benchmark-tcp.json`
  - `benchmarks/configs/benchmark-shm.json`
- PDU workloads:
  - `disturb`
  - `lidar_points`
  - `hako_camera_data`
- Protocols:
  - TCP
  - SHM(callback)
- Callback benchmark setting:
  - `recv_cache_write=false`
  - The same callback-only receive condition used in the macOS baseline was used for the Windows validation runs.
- Build/runtime notes:
  - built with `-EnableHakoniwaCore`
  - `-HakoniwaCoreRoot` pointed to a Hakoniwa Core install prefix
  - Hakoniwa Core DLLs were made visible on `PATH`
  - host CPU and memory metadata were not captured in the current run log set

### Correctness

All native Windows runs completed without loss.

| PDU name | Protocol | sent | received | completed | loss_count |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 1024 | 1024 | 1 | 0 |
| `disturb` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `lidar_points` | TCP | 1024 | 1024 | 1 | 0 |
| `lidar_points` | SHM(callback) | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | TCP | 1024 | 1024 | 1 | 0 |
| `hako_camera_data` | SHM(callback) | 1024 | 1024 | 1 | 0 |

### Timing Publication Status

The current repository log set proves that all six native Windows runs completed successfully, but it is not yet suitable for external publication as an independent timing dataset. The recorded Windows benchmark logs numerically match the existing reference values already used elsewhere in this report, while the host CPU and memory metadata were not captured alongside the run. Publishing those timings as a separate Windows performance table would therefore be too easy to misread as copied data rather than an independently documented rerun.

For that reason, this section is intentionally limited to execution validation. A clean rerun with explicit host metadata capture should be used before publishing standalone native Windows timing, throughput, or relative-performance tables.

### Native Windows Notes

The native Windows result matches the benchmark logs collected under:

- `benchmarks/logs/disturb/`
- `benchmarks/logs/lidar_points/`
- `benchmarks/logs/hako_camera_data/`

Representative execution summaries:

- `BENCH_PUB_SUMMARY protocol=tcp expected=1024 sent=1024 send_duration_ms=10.0314`
- `BENCH_SUB_SUMMARY protocol=tcp expected=1024 received=1024 recv_span_ms=9.91863 completed=1`
- `BENCH_PUB_SUMMARY protocol=shm expected=1024 sent=1024 send_duration_ms=5.23071`
- `BENCH_SUB_SUMMARY protocol=shm expected=1024 received=1024 recv_span_ms=2.21146 completed=1`

- `BENCH_PUB_SUMMARY protocol=tcp expected=1024 sent=1024 send_duration_ms=1576.27`
- `BENCH_SUB_SUMMARY protocol=tcp expected=1024 received=1024 recv_span_ms=1566.73 completed=1`
- `BENCH_PUB_SUMMARY protocol=shm expected=1024 sent=1024 send_duration_ms=94.6776`
- `BENCH_SUB_SUMMARY protocol=shm expected=1024 received=1024 recv_span_ms=130.965 completed=1`

Logs:

- `benchmarks/logs/disturb/benchmark-tcp_pub.log`
- `benchmarks/logs/disturb/benchmark-tcp_sub.log`
- `benchmarks/logs/disturb/benchmark-shm_pub.log`
- `benchmarks/logs/disturb/benchmark-shm_sub.log`
- `benchmarks/logs/lidar_points/benchmark-tcp_pub.log`
- `benchmarks/logs/lidar_points/benchmark-tcp_sub.log`
- `benchmarks/logs/lidar_points/benchmark-shm_pub.log`
- `benchmarks/logs/lidar_points/benchmark-shm_sub.log`
- `benchmarks/logs/hako_camera_data/benchmark-tcp_pub.log`
- `benchmarks/logs/hako_camera_data/benchmark-tcp_sub.log`
- `benchmarks/logs/hako_camera_data/benchmark-shm_pub.log`
- `benchmarks/logs/hako_camera_data/benchmark-shm_sub.log`

This confirms that the TCP and SHM benchmark paths are runnable end-to-end on native Windows with Hakoniwa Core present and configured correctly. Independent native Windows performance publication should wait for a clean rerun with host metadata capture.
