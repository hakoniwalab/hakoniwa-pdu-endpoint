# C# Binding Design

## Goal

Add a C# binding for `hakoniwa-pdu-endpoint` without introducing a second runtime model.
The C# layer should follow the same architectural boundary as the Python binding:

- stable native boundary at the C facade
- thin language wrapper over that facade

This keeps the core C++ library language-neutral and prevents engine-specific concerns from leaking into the native layer.

## Target Environments

The primary targets are:

- Unity
- Godot

These environments strongly prefer:

- non-blocking main-thread behavior
- explicit control over when callbacks are delivered
- avoidance of engine API calls from arbitrary transport threads

That makes a direct native-callback-to-user-handler model unsafe as the default, and it also argues for native/C-owned pending receive state rather than managed callback reconstruction.

## Layering

Recommended structure:

1. `NativeEndpoint`
   - P/Invoke declarations for `include/hakoniwa/pdu/c_endpoint.h`
   - owns the native handle lifetime
2. `Endpoint`
   - thin managed wrapper over the C facade
   - mirrors `open/start/post_start/stop/close/send/recv/...`

This mirrors the current Python structure:

- wrapper and callback convenience: `python/hakoniwa_pdu_endpoint/c_endpoint.py`

## Standard Receive Model

The standard C# receive path should be pull-based.

Recommended model:

- native/C layer owns pending receive state
- C# consumes pending events through `RecvNext(...)`
- higher layers invoke user handlers only from caller-controlled code paths
- explicit event registration in the C facade is optional, not required for plain receive APIs

This means the binding should not rely on direct native-to-managed callback delivery as the primary engine-facing receive model.

Existing native callback registration may remain available as a low-level or compatibility API, but it should not define the standard Unity/Godot integration story.

## Event Delivery Model

Recommended per-frame pattern:

1. call `ProcessRecvEvents()` when the underlying comm needs polling
2. call `GetPendingCount()`
3. call `RecvNext(...)` for the pending records you want to consume
3. invoke user handlers from the engine main thread

This keeps transport-facing activity and engine-facing callback execution separate without rebuilding transport semantics in C#.

## `process_recv_events()` vs Pull/Drain

These are different responsibilities and should remain separate:

- `process_recv_events()`
  - advances native-side receive handling for poll-based comm implementations
  - especially relevant for SHM poll integration
- `GetPendingCount()` / `RecvNext(...)`
  - consumes already-pending receive state
  - lets the caller invoke user handlers on its own thread

Typical engine integration:

- Unity: call both from `Update()`
- Godot: call both from `_Process(double delta)`

## Transport-Independent Receive Model

The public C# model should not depend on which transport produced the receive event.

That includes:

- TCP receive threads
- UDP receive threads
- WebSocket receive callbacks
- MQTT and Zenoh receive callbacks
- SHM callback mode
- SHM poll mode

From the C# caller's point of view, all of these should normalize into the same behavior:

- native side receives data
- native/C layer records pending receive state
- the application explicitly drains pending events on its chosen thread

This means transport callback timing must not leak into the public managed callback model.

## Why Explicit Main-Thread Dispatch

The safe default is:

- background/native threads may receive data
- native/C layer may mark receive state pending
- user handlers run only when the application explicitly pulls and dispatches events

## Memory and Lifetime Rules

The C# binding should enforce these rules:

- keep native callback delegates alive for as long as native code may call them, if callback APIs are used
- use `GCHandle` for `user_data` style association when needed
- release native handles deterministically
- stop callback delivery before tearing down managed state

Recommended implementation details:

- use `SafeHandle` for the native endpoint handle
- keep callback delegates in instance fields when exposing compatibility callbacks
- model pulled payloads as `byte[]`

## Native/C-Owned Pending Receive State

The native endpoint layer should expose transport-independent pending receive state through the C facade.

See also:

- `docs/receive_semantics.md`

Reason:

- TCP/UDP/WebSocket/MQTT/Zenoh receive on background/native callback paths
- SHM callback mode also receives on callback paths
- SHM poll mode advances receive by explicit `process_recv_events()`

These should all converge to one native-side receive model before reaching C#.

Important implications:

- pending receive state must be defined by native cache/runtime semantics
- `recv_next(...)` is the primary binding-facing receive API
- `get_pending_count(...)` is the standard loop-control API
- `recv(key, ...)` and `recv_next(...)` must remain consistent
- event registration should be opt-in and transport-independent from the binding point of view
- callback registration in C# should be treated as low-level compatibility, not the core model

## Threading Rules

The intended threading model is:

- transport or native callback threads may make receive state pending
- no engine API access occurs on those threads
- the engine main thread pulls and dispatches events
- polling comms are advanced only when the application calls `ProcessRecvEvents()`

This is consistent with the project's broader design choice of explicit integration control over hidden scheduling.

## API Direction

Minimal expected managed APIs:

- `Endpoint`
  - `Open`
  - `CreatePduLchannels`
  - `Start`
  - `PostStart`
  - `Stop`
  - `Close`
  - `IsRunning`
  - `ProcessRecvEvents`
  - `Send`
  - `SendByName`
  - `SetRecvEvent`
  - `GetPendingCount`
  - `Recv`
  - `RecvByName`
  - `RecvNext`
  - `GetPduSize`
  - `GetPduChannelId`
  - `GetPduName`

## Non-Goals For v1

- no direct exposure of `EndpointCommMultiplexer`
- no automatic engine-specific integration layer in the native library
- no hidden background dispatch thread as the default delivery model
- no attempt to bypass the existing C facade with direct C++ interop

## Design Summary

The C# binding should reuse the same boundary already established for Python:

- C++ core stays unchanged
- C facade remains the ABI contract
- native/C layer owns pending receive semantics
- C# adds a thin wrapper over that model
- user callbacks are delivered only from caller-controlled pull/drain code paths

This is the safest model for Unity and Godot and is consistent with the project's explicit scheduling philosophy.
