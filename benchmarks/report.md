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

Hakoniwa supports cross-platform deployment. These measurements were
collected independently on each platform; no single platform is treated
as a reference target.

Hakoniwa is an open-source simulation platform for cyber-physical systems, and this benchmark measures batch-level effective performance through its PDU endpoint layer. It is not a bare TCP or bare shared-memory microbenchmark. The measured path includes endpoint send/receive behavior, PDU key handling, transport framing or shared-memory access, callback dispatch, timestamp capture, and in-memory benchmark log buffering.

This report evaluates batch-level effective throughput of the Hakoniwa PDU endpoint layer, not raw transport bandwidth. The results should be interpreted as endpoint-level behavior under the tested workload and platform conditions.

## Workload

All measurements use 1024 PDU sends.

| PDU name | Type | Channel ID | PDU size | Total payload |
| --- | --- | ---: | ---: | ---: |
| `disturb` | `hako_msgs/Disturbance` | 3 | 256 bytes | 262,144 bytes |
| `lidar_points` | `sensor_msgs/PointCloud2` | 16 | 177,424 bytes | 181,682,176 bytes |
| `hako_camera_data` | `hako_msgs/HakoCameraData` | 12 | 1,002,992 bytes | 1,027,063,808 bytes |

The send key pattern is `Drone-1/<pdu_name>` through `Drone-1024/<pdu_name>`.

Repository logs are now separated by environment under:

- `benchmarks/logs/mac/`
- `benchmarks/logs/lnx/`
- `benchmarks/logs/win/`

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

The macOS result benefits from Apple Silicon's Unified Memory
architecture, where CPU processes share the same physical memory pool
with low-coherency overhead. This gives the SHM(callback) path
particularly high effective bandwidth on M-series hardware. Ubuntu
(WSL2) and Windows results reflect their respective OS shared-memory
APIs and, in the WSL2 case, the additional virtualization layer.

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
| `disturb` | TCP | 2.649 | 2.569 | 2.650 | 0.001 |
| `disturb` | SHM(callback) | 5.691 | 8.030 | 8.139 | 2.448 |
| `lidar_points` | TCP | 100.740 | 100.375 | 100.839 | 0.099 |
| `lidar_points` | SHM(callback) | 65.834 | 65.605 | 65.849 | 0.014 |
| `hako_camera_data` | TCP | 1073.080 | 1071.230 | 1073.523 | 0.447 |
| `hako_camera_data` | SHM(callback) | 333.972 | 332.526 | 334.051 | 0.079 |

### Throughput Results

Throughput uses decimal units. Some rows are shown in MB/s and others in GB/s for readability.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 99.0 MB/s | 98.9 MB/s |
| `disturb` | SHM(callback) | 46.1 MB/s | 32.2 MB/s |
| `lidar_points` | TCP | 1.80 GB/s | 1.80 GB/s |
| `lidar_points` | SHM(callback) | 2.76 GB/s | 2.76 GB/s |
| `hako_camera_data` | TCP | 957.1 MB/s | 956.7 MB/s |
| `hako_camera_data` | SHM(callback) | 3.08 GB/s | 3.07 GB/s |

### Relative Performance

`SHM/TCP speedup` is defined as `TCP time / SHM time`. Values above `1.0` favor SHM(callback); values below `1.0` favor TCP.

| PDU name | SHM/TCP end-to-end speedup | SHM/TCP publisher send speedup |
| --- | ---: | ---: |
| `disturb` | 0.33x | 0.47x |
| `lidar_points` | 1.53x | 1.53x |
| `hako_camera_data` | 3.21x | 3.21x |

### Ubuntu Notes

The Ubuntu result preserves the same qualitative behavior as the macOS baseline and the native Windows measurement addendum for larger payloads: TCP send and receive progress stay tightly coupled, while SHM(callback) completes the same batch faster for `lidar_points` and `hako_camera_data`.

The Ubuntu and native Windows sections report the same Intel(R) Core(TM) Ultra 7 155H host, but the reported core topology differs because the Linux/WSL2 and Windows system tools use different topology-reporting conventions.

For the smallest workload, `disturb`, this Ubuntu run shows the opposite result: TCP is faster than SHM(callback). In the current SHM(callback) path, fixed per-batch control costs such as callback dispatch, conductor-driven pacing, and the first-receive synchronization path are large relative to the 256-byte payload, so the shared-memory data path does not amortize those costs well in this case.

The current environment-separated `lnx` logs are now the source of truth for Ubuntu/WSL2 publication. For `hako_camera_data`, the present Ubuntu run is faster than the macOS baseline for TCP at 1073.523 ms versus 1576.351 ms, but slower for SHM(callback) at 334.051 ms versus 132.303 ms.

The lower SHM(callback) speedup compared to the macOS baseline is
expected: the WSL2 virtualization layer and the Linux shared-memory
API do not benefit from Apple Silicon's Unified Memory architecture.

## Native Windows Measurement Addendum

Additional measurements were collected on native Windows on June 7, 2026 for the same three benchmark workloads with 1024 sends.

### Environment

- OS: native Windows
- CPU: Intel(R) Core(TM) Ultra 7 155H
- CPU topology: 1 socket, 16 cores, 22 threads
- Cache:
  - L2: 18 MiB
  - L3: 24 MiB
- Memory: 64 GiB RAM
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

### Timing Results

| PDU name | Protocol | send_duration_ms | recv_span_ms | end_to_end_ms | tail_after_send_ms |
| --- | --- | ---: | ---: | ---: | ---: |
| `disturb` | TCP | 13.187 | 13.073 | 13.188 | 0.002 |
| `disturb` | SHM(callback) | 12.459 | 23.413 | 26.050 | 13.591 |
| `lidar_points` | TCP | 316.392 | 315.939 | 316.495 | 0.104 |
| `lidar_points` | SHM(callback) | 88.431 | 85.508 | 88.483 | 0.051 |
| `hako_camera_data` | TCP | 1584.530 | 1589.210 | 1590.572 | 6.046 |
| `hako_camera_data` | SHM(callback) | 455.080 | 451.022 | 455.483 | 0.403 |

### Throughput Results

Throughput uses decimal units. Some rows are shown in MB/s and others in GB/s for readability.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 19.9 MB/s | 19.9 MB/s |
| `disturb` | SHM(callback) | 21.0 MB/s | 10.1 MB/s |
| `lidar_points` | TCP | 574.2 MB/s | 574.0 MB/s |
| `lidar_points` | SHM(callback) | 2.05 GB/s | 2.05 GB/s |
| `hako_camera_data` | TCP | 648.2 MB/s | 645.7 MB/s |
| `hako_camera_data` | SHM(callback) | 2.26 GB/s | 2.25 GB/s |

### Relative Performance

`SHM/TCP speedup` is defined as `TCP time / SHM time`. Values above `1.0` favor SHM(callback); values below `1.0` favor TCP.

| PDU name | SHM/TCP end-to-end speedup | SHM/TCP publisher send speedup |
| --- | ---: | ---: |
| `disturb` | 0.51x | 1.06x |
| `lidar_points` | 3.58x | 3.58x |
| `hako_camera_data` | 3.49x | 3.48x |

### Native Windows Notes

The native Windows result is now taken from the environment-separated logs under:

- `benchmarks/logs/win/disturb/`
- `benchmarks/logs/win/lidar_points/`
- `benchmarks/logs/win/hako_camera_data/`

Representative execution summaries:

- `BENCH_PUB_SUMMARY protocol=tcp expected=1024 sent=1024 send_duration_ms=13.1869`
- `BENCH_SUB_SUMMARY protocol=tcp expected=1024 received=1024 recv_span_ms=13.0727 completed=1`
- `BENCH_PUB_SUMMARY protocol=shm expected=1024 sent=1024 send_duration_ms=12.4594`
- `BENCH_SUB_SUMMARY protocol=shm expected=1024 received=1024 recv_span_ms=23.4128 completed=1`

- `BENCH_PUB_SUMMARY protocol=tcp expected=1024 sent=1024 send_duration_ms=1584.53`
- `BENCH_SUB_SUMMARY protocol=tcp expected=1024 received=1024 recv_span_ms=1589.21 completed=1`
- `BENCH_PUB_SUMMARY protocol=shm expected=1024 sent=1024 send_duration_ms=455.08`
- `BENCH_SUB_SUMMARY protocol=shm expected=1024 received=1024 recv_span_ms=451.022 completed=1`

Logs:

- `benchmarks/logs/win/disturb/benchmark-tcp_pub.log`
- `benchmarks/logs/win/disturb/benchmark-tcp_sub.log`
- `benchmarks/logs/win/disturb/benchmark-shm_pub.log`
- `benchmarks/logs/win/disturb/benchmark-shm_sub.log`
- `benchmarks/logs/win/lidar_points/benchmark-tcp_pub.log`
- `benchmarks/logs/win/lidar_points/benchmark-tcp_sub.log`
- `benchmarks/logs/win/lidar_points/benchmark-shm_pub.log`
- `benchmarks/logs/win/lidar_points/benchmark-shm_sub.log`
- `benchmarks/logs/win/hako_camera_data/benchmark-tcp_pub.log`
- `benchmarks/logs/win/hako_camera_data/benchmark-tcp_sub.log`
- `benchmarks/logs/win/hako_camera_data/benchmark-shm_pub.log`
- `benchmarks/logs/win/hako_camera_data/benchmark-shm_sub.log`

The lower SHM(callback) speedup compared to the macOS baseline is
expected: Windows named shared-memory APIs do not benefit from Apple
Silicon's Unified Memory architecture, and the IPC path carries
additional OS overhead relative to the macOS baseline.

This confirms that the TCP and SHM benchmark paths are runnable end-to-end on native Windows with Hakoniwa Core present and configured correctly. The logs are now separated by environment, so the Windows values above are no longer conflated with macOS or WSL2 results.
