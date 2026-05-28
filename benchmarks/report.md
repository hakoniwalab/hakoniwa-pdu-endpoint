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
| `disturb` | TCP | 13.884 | 13.780 | 13.919 | 0.035 |
| `disturb` | SHM(callback) | 6.155 | 10.023 | 10.723 | 4.567 |
| `lidar_points` | TCP | 332.172 | 330.300 | 332.212 | 0.040 |
| `lidar_points` | SHM(callback) | 20.551 | 46.706 | 52.150 | 31.599 |
| `hako_camera_data` | TCP | 1723.870 | 1713.780 | 1724.045 | 0.173 |
| `hako_camera_data` | SHM(callback) | 73.046 | 212.053 | 221.201 | 148.155 |

## Throughput Results

Throughput uses decimal MB/s.

| PDU name | Protocol | Publisher send throughput | End-to-end throughput |
| --- | --- | ---: | ---: |
| `disturb` | TCP | 18.9 MB/s | 18.8 MB/s |
| `disturb` | SHM(callback) | 42.6 MB/s | 24.4 MB/s |
| `lidar_points` | TCP | 547.0 MB/s | 546.9 MB/s |
| `lidar_points` | SHM(callback) | 8.84 GB/s | 3.48 GB/s |
| `hako_camera_data` | TCP | 595.8 MB/s | 595.7 MB/s |
| `hako_camera_data` | SHM(callback) | 14.1 GB/s | 4.64 GB/s |

## Relative Performance

| PDU name | End-to-end speedup | Publisher send speedup |
| --- | ---: | ---: |
| `disturb` | SHM is 1.30x faster | SHM is 2.26x faster |
| `lidar_points` | SHM is 6.37x faster | SHM is 16.16x faster |
| `hako_camera_data` | SHM is 7.79x faster | SHM is 23.60x faster |

## Metric Notes

- `send_duration_ms`: Time from the publisher starting the first send until it finishes the final send.
- `recv_span_ms`: Time from the subscriber's first receive callback until its final receive callback.
- `end_to_end_ms`: Time from the publisher starting the batch until the subscriber receives the final PDU.
- `tail_after_send_ms`: Time from publisher send completion until subscriber final receive completion.

`end_to_end_ms` is the main whole-batch completion metric. `send_duration_ms` isolates publisher-side send/write path cost. `tail_after_send_ms` shows how much subscriber-side work remains after the publisher has already finished sending.

## Analysis

TCP shows a consistent pattern across all PDU sizes: `send_duration_ms` and `recv_span_ms` are nearly the same, and `tail_after_send_ms` is very small.

This indicates that TCP backpressure keeps the publisher and subscriber moving almost synchronously. The publisher is naturally pulled by the subscriber's receive progress, so little receive backlog remains after the publisher finishes sending.

SHM(callback) shows a different pattern. The publisher send path is much faster, especially for large PDU payloads. For `hako_camera_data`, SHM(callback) sends about 1.027 GB in 73.046 ms, while TCP takes 1723.870 ms.

However, SHM(callback) also shows a larger `tail_after_send_ms`. For `hako_camera_data`, the publisher finishes quickly, but subscriber-side callback dispatch and receive processing continue for another 148.155 ms.

This means SHM(callback) has a very strong write/send path, but large batches can leave subscriber callback processing visible as tail latency.

## Summary

- TCP and SHM(callback) both delivered all tested PDUs without loss.
- SHM(callback) outperformed TCP end-to-end for all tested PDU sizes.
- The SHM(callback) advantage becomes much larger as PDU size grows.
- TCP behaves as a synchronized streaming pipeline due to backpressure.
- SHM(callback) lets the publisher run far ahead, so subscriber callback processing appears as `tail_after_send_ms`.
- For large local IPC payloads, SHM(callback) provides much higher throughput, while TCP provides a smaller tail and more synchronized pub/sub progress.
