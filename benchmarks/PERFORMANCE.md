# Hakoniwa PDU Endpoint: Performance Characteristics and Design Rationale

[English](PERFORMANCE.md) | [日本語](PERFORMANCE.ja.md)

---

## What is Hakoniwa

Hakoniwa is an open-source simulation platform and hub for
cyber-physical systems. Its role is to connect heterogeneous
simulation models — flight
controllers, sensor models, visualization tools, and external
applications — through a shared virtual environment. Communication
between models passes through the Hakoniwa PDU endpoint layer, which
supports both TCP and shared-memory (SHM) backends.

---

## Why transport performance matters

In a simulation hub, the dominant workload is not small control
signals but large sensor payloads: camera images (~1 MB per frame)
and LiDAR point clouds (~177 KB per scan) that must be delivered
every simulation step.

Equally important is the delivery pattern. A single sensor publisher
typically feeds multiple subscribers simultaneously — a flight
controller, an obstacle detector, a visualizer, and so on. This is
a 1:n delivery pattern. TCP requires proportionally more publisher-side
data transfer per additional subscriber. SHM(callback) writes the
payload once to shared memory and then notifies subscribers, so it can
scale better on the publisher side as n grows. The benchmark results
in `report.md` measure a 1:1 configuration and therefore do not
capture this 1:n scaling behavior.

---

## Benchmark overview

Full measurement details, raw timing tables, and per-platform
environment specifications are in
[`report.md`](report.md).

The table below summarizes end-to-end SHM(callback) speedup over TCP
across three platforms and three payload sizes.

| PDU | Payload | macOS (M2 Pro) | Ubuntu (WSL2) | Windows |
| --- | ---: | ---: | ---: | ---: |
| `disturb` | 256 B | 1.64x | 0.33x | 0.51x |
| `lidar_points` | 177 KB | 10.26x | 1.53x | 3.58x |
| `hako_camera_data` | 1 MB | 11.91x | 3.21x | 3.49x |

Values above 1.0 favor SHM(callback); values below 1.0 favor TCP.
All runs completed without loss across all platforms.

![SHM(callback) end-to-end speedup over TCP](results/shm_tcp_speedup.svg)

---

## Key findings

- For large payloads, SHM(callback) substantially outperforms TCP on
  all tested platforms, reaching up to 11.91x end-to-end speedup and
  16.65x publisher-send speedup on macOS under the tested 1:1
  benchmark condition.
- The macOS result benefits from Apple Silicon's Unified Memory
  architecture, where CPU processes share the same physical memory
  pool with low-coherency overhead. Ubuntu (WSL2) and Windows reflect
  their respective OS shared-memory APIs and, in the WSL2 case, the
  additional virtualization layer.
- For small payloads (256 bytes), SHM(callback) is slower than TCP on
  Ubuntu and Windows. This is expected behavior, not a defect.
- The benchmark measures 1:1 delivery. Hakoniwa's primary 1:n use case
  may further favor SHM(callback) on the publisher side because the
  payload is written once to shared memory instead of sent separately
  over multiple TCP streams.

---

## Why small payloads favor TCP: implementation perspective

The SHM(callback) path involves several fixed costs that are
independent of payload size:

- **Receive-event dispatch**: `call_recv_event_callbacks()` performs
  a linear scan over all registered receive-event entries. With 1024
  channels active in the benchmark, this scan dominates for small
  payloads.
- **Shared-memory readback copy**: the callback path reads the PDU
  back from the shared-memory slot into a receive buffer before
  invoking the user callback. This is not a zero-copy delivery.
- **Synchronization overhead**: in the current implementation, each
  receive involves atomic flag checks, mutex acquisition, and PDU
  definition resolution.
- **Benchmark accounting**: per-message timestamp capture and log
  buffering add a small but non-negligible fixed cost shared by both
  backends.

For large payloads these costs are amortized over the payload
transfer time. For 256-byte payloads, they dominate. This is a
structural characteristic of the current implementation, not a
performance bug.

The benchmark accounting costs apply to both backends, but the
SHM(callback)-specific dispatch and shared-memory readback costs
become visible when the payload is only 256 bytes.

---

## Design rationale

Hakoniwa's transport choice is guided by payload size and delivery
pattern, not by a single preferred backend.

TCP is appropriate for small control signals and command messages.
Its backpressure-driven flow keeps publisher and subscriber
synchronized, and its fixed costs are low relative to the payload.

SHM(callback) is appropriate for large sensor payloads, especially
when a publisher feeds multiple subscribers. Writing once to shared
memory can reduce publisher-side data transfer compared with sending n
TCP streams, while subscriber-side callback dispatch and readback work
still scale with subscriber count. The high memory bandwidth available
on all tested platforms makes large-payload transfer fast.

The SHM(callback) performance reported here is measured in a 1:1
configuration. Production simulation scenarios with multiple
subscribers per sensor channel should be evaluated separately, but the
transport design is intended to make large 1:n sensor delivery more
efficient than repeated TCP payload transmission from the publisher.

---

## Future directions

The following improvements have been identified and may be addressed
in future releases:

- **Receive-event indexing**: replace the linear scan in
  `call_recv_event_callbacks()` with a direct index from channel ID
  to event entry, reducing dispatch cost for large channel counts.
- **Callback fast path**: resolve PDU definitions and endpoint
  metadata at subscription time rather than per-receive, eliminating
  repeated map lookups and resolve calls on the hot path.
- **Zero-copy callback API**: pass a shared-memory view directly to
  the callback instead of copying into a receive buffer. This
  requires careful lifetime and synchronization design and would be
  introduced as a separate opt-in API.
- **Benchmark summary mode**: make per-message logging optional so
  that throughput measurements can separate transport cost from
  logging cost.
