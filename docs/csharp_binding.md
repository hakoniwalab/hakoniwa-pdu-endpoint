# C# Binding Design

## Goal

Add a C# binding for `hakoniwa-pdu-endpoint` without introducing a second runtime model.
The C# layer should follow the same architectural boundary as the Python binding:

- stable native boundary at the C facade
- thin language wrapper over that facade
- higher-level async/event wrapper above the thin binding

This keeps the core C++ library language-neutral and prevents engine-specific concerns from leaking into the native layer.

## Target Environments

The primary targets are:

- Unity
- Godot

These environments strongly prefer:

- non-blocking main-thread behavior
- explicit control over when callbacks are delivered
- avoidance of engine API calls from arbitrary transport threads

That makes a direct native-callback-to-user-handler model unsafe as the default.

## Layering

Recommended structure:

1. `NativeEndpoint`
   - P/Invoke declarations for `include/hakoniwa/pdu/c_endpoint.h`
   - owns the native handle lifetime
2. `Endpoint`
   - thin managed wrapper over the C facade
   - mirrors `open/start/post_start/stop/close/send/recv/...`
3. `EndpointAsync`
   - callback-safe event capture
   - copies payloads immediately
   - queues managed events for later dispatch

This mirrors the current Python structure:

- thin wrapper: `python/hakoniwa_pdu_endpoint/c_endpoint.py`
- async wrapper: `python/hakoniwa_pdu_endpoint/c_endpoint_async.py`

## Callback Model

The native callback should do the minimum possible work:

- resolve the managed receiver instance
- copy the payload into managed memory
- enqueue a `PduEvent`
- return immediately

The native callback must not:

- block
- call Unity or Godot APIs
- run user handlers directly
- keep borrowed native payload pointers after return

This follows the existing C facade contract: callback payload pointers are borrowed only for the duration of the callback.

## Event Delivery Model

`EndpointAsync` should expose explicit draining of queued events from managed code.

Recommended per-frame pattern:

1. call `ProcessRecvEvents()` when the underlying comm needs polling
2. call `DrainPending()` on the async wrapper
3. invoke user handlers from the engine main thread

This keeps transport-facing activity and engine-facing callback execution separate.

## `process_recv_events()` vs `DrainPending()`

These are different responsibilities and should remain separate:

- `process_recv_events()`
  - advances native-side receive handling for poll-based comm implementations
  - especially relevant for SHM poll integration
- `DrainPending()`
  - drains already-captured managed events from the async queue
  - invokes user handlers on the caller's thread

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
- managed side records a pending receive event
- the application explicitly drains pending events on its chosen thread

This means native callback timing must not leak into the public managed callback model.

## Why Explicit Main-Thread Dispatch

Using a dedicated managed dispatch thread is possible, but should not be the default for engine integrations.

Reason:

- Unity objects are generally main-thread bound
- Godot scene-tree interactions are generally main-thread bound
- handler code often evolves to include engine API access, even if it starts as pure data handling

Therefore the safe default is:

- background/native threads may receive data
- managed async wrapper may queue data
- user handlers run only when the application explicitly pumps the queue

## Memory and Lifetime Rules

The C# binding should enforce these rules:

- keep native callback delegates alive for as long as native code may call them
- use `GCHandle` for `user_data` style association when needed
- copy callback payload bytes before returning from the native callback
- release native handles deterministically
- stop callback delivery before tearing down managed dispatch state

Recommended implementation details:

- use `SafeHandle` for the native endpoint handle
- keep callback delegates in instance fields
- model payloads as `byte[]` in queued events

## Future Direction: Cache-Backed Pending Receive State

The current C# scaffold keeps its own managed queue above the C facade.
That is sufficient for an initial binding, but the more robust long-term design is to let the native endpoint layer expose transport-independent pending receive state.

See also:

- `docs/receive_semantics.md`

The reason is consistency:

- TCP/UDP/WebSocket/MQTT/Zenoh receive on background/native callback paths
- SHM callback mode also receives on callback paths
- SHM poll mode advances receive by explicit `process_recv_events()`

These should all converge to one native-side receive model before reaching C#.

### Why Cache Is Relevant

The endpoint already writes received data into cache before notifying subscribers.
That makes cache the natural place to extend receive semantics.

However, the current cache abstraction is key-based storage, not a full pending-event abstraction.

Important distinction:

- `latest` cache is state-oriented
- `queue` cache is per-key FIFO storage

Neither alone provides a transport-independent global pending receive feed.

### Proposed Direction

Add pending receive event handling alongside cache semantics.

Both cache modes would gain arrival-order tracking, but with different meaning:

- `latest`
  - keep only the latest payload per key
  - keep at most one pending receive entry per key
  - `recv_next` returns the next pending key in arrival order
  - payload is then resolved from the latest stored value

- `queue`
  - keep multiple payloads per key
  - keep arrival-order receive entries for every accepted payload
  - `recv_next` consumes events in arrival order
  - payload/event correspondence remains queue-like

### Required Consistency Rule

Receive-event registration must happen if and only if payload acceptance succeeded.

In other words:

- if payload storage fails, no receive event may be queued
- if payload storage succeeds, receive-event state must be updated in the same critical section

This prevents contradictions between visible receive events and readable payload data.

### Interaction With `recv(key, ...)`

This design must also handle applications that do not use event-driven receive.

If an application consumes data directly through `recv(key, ...)`, pending receive state must remain consistent.

That means:

- reading by key may need to consume one pending receive indication for that key
- `recv_next(...)` and `recv(key, ...)` must not drift apart over time

This is especially important if both APIs are used in the same process.

### Design Implication

Because of that consistency requirement, pending receive handling should not be implemented as a loose side queue outside cache semantics.

It should be integrated with cache read/write behavior so that:

- write updates payload state and receive-event state together
- key-based reads and arrival-order reads consume state consistently

This is the direction that best matches the project's explicit semantics and transport-independent endpoint model.

## Threading Rules

The intended threading model is:

- transport or native callback threads may enqueue events
- no engine API access occurs on those threads
- the engine main thread drains and dispatches events
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
  - `Recv`
  - `RecvByName`
  - `RecvNext`
  - `GetPduSize`
  - `GetPduChannelId`
  - `GetPduName`

- `EndpointAsync`
  - `OnRecv`
  - `OnRecvByName`
  - `DrainPending`
  - optional `TryDequeue`

## Non-Goals For v1

- no direct exposure of `EndpointCommMultiplexer`
- no automatic engine-specific integration layer in the native library
- no hidden background dispatch thread as the default delivery model
- no attempt to bypass the existing C facade with direct C++ interop

## Design Summary

The C# binding should reuse the same boundary already established for Python:

- C++ core stays unchanged
- C facade remains the ABI contract
- C# adds a thin wrapper plus an async queue layer
- user callbacks are delivered explicitly on the main thread

This is the safest model for Unity and Godot and is consistent with the project's explicit scheduling philosophy.
