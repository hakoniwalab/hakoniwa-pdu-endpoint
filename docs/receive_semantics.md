# Runtime Receive Semantics

This document describes the intended runtime receive semantics for `Endpoint` and cache-backed receive state.

This is distinct from `StorageComm`.

- `StorageComm` describes persistent files and replay/state-snapshot semantics
- this document describes in-memory runtime receive behavior across transports

## Problem Statement

Different transports currently surface receive behavior differently:

- TCP/UDP/WebSocket/MQTT/Zenoh may deliver via transport-managed threads or callbacks
- SHM poll advances receive only when `process_recv_events()` is called
- SHM callback delivers through the Hakoniwa callback path

From the application point of view, these differences should not force different receive models.

The runtime should normalize them into one explicit receive model.

## Scope

This document is about:

- runtime endpoint receive behavior
- cache-backed receive state
- interaction between `recv(key, ...)`, callback delivery, and `recv_next(...)`

This document is not about:

- storage file formats
- storage replay semantics
- transport-native threading internals

## Design Goal

The core runtime should present transport-independent receive semantics:

- data is accepted by the runtime
- receive state becomes pending
- applications may consume that state explicitly

Language bindings such as Python and C# should not need to reconstruct receive meaning independently for each transport.

## Normalized Receive Model

The intended model is:

1. transport receives data
2. endpoint accepts the payload
3. cache-backed receive state is updated
4. optional callback notification may occur
5. application consumes receive state through explicit APIs

The important point is that transport callback timing and receive-state consumption are different concerns.

## Cache Modes

Two runtime cache modes exist today:

- `latest`
- `queue`

They should both support runtime receive state, but not with identical meaning.

## `latest` Semantics

`latest` is state-oriented.

Meaning:

- only the newest payload per key is retained
- intermediate updates for the same key may be overwritten
- pending receive state is per key, not per payload instance

Proposed receive behavior:

- payload store:
  - one latest value per `(robot, channel_id)`
- pending receive state:
  - at most one pending receive entry per key
- arrival order:
  - `recv_next(...)` returns keys in the order they first became pending

Implication:

- `recv_next(...)` in `latest` is a pending-key API, not a lossless event-log API
- after multiple updates to the same key, the returned payload is the current latest value

## `queue` Semantics

`queue` is event-oriented.

Meaning:

- multiple payloads per key may be retained
- arrival order matters
- repeated updates to the same key remain distinct events while retained

Proposed receive behavior:

- payload store:
  - per-key FIFO storage
- pending receive state:
  - one pending receive entry per accepted payload
- arrival order:
  - `recv_next(...)` returns events in global arrival order

Implication:

- `queue` is the mode for applications that need runtime event-style receive semantics
- repeated keys are expected and correct

## Required Consistency Rule

Payload acceptance and receive-event registration must be consistent.

Required invariant:

- a receive event exists if and only if the payload was accepted into runtime state

Therefore:

- if payload storage fails, no pending receive entry may be created
- if payload storage succeeds, pending receive state must be updated in the same critical section

This prevents visible receive events from pointing at unreadable or already-rejected payload state.

## Interaction With `recv(key, ...)`

The design must support applications that do not use event-driven receive and only call `recv(key, ...)`.

That means pending receive state cannot be managed only by `recv_next(...)`.

Required behavior:

- `recv(key, ...)` must consume receive state consistently for that key
- `recv_next(...)` must consume receive state consistently in arrival order
- mixing these APIs must not cause unbounded drift between payload state and pending receive state

Exact consequences depend on cache mode.

### In `latest`

`recv(key, ...)` should clear the pending state for that key.

Reason:

- pending state is per key
- once the current latest value has been read explicitly, that key should no longer remain pending

### In `queue`

`recv(key, ...)` should consume one queued payload for that key and must also consume the corresponding pending receive entry.

Reason:

- otherwise receive-event metadata would remain stale
- pending arrival order would diverge from the actual payload queue

## Callbacks

Callbacks are notifications, not the receive-state model itself.

Recommended runtime meaning:

- callback delivery informs the application that receive state became pending
- callback delivery does not by itself define payload consumption

This keeps callback-based transports and poll-based transports compatible with the same explicit read model.

## `process_recv_events()`

`process_recv_events()` is not a general receive API.

Current meaning:

- meaningful for SHM poll
- no-op for transports that do not require explicit polling

Its job is:

- advance native-side receive detection where polling is required

Its job is not:

- define how receive state is consumed

## Runtime Receive vs Storage Receive

`recv_next(...)` already exists in `StorageComm`, but its meaning is different.

`StorageComm`:

- `queue` mode is a replay log
- repeated keys are expected
- ordering is persistent log order

Runtime cache-backed receive:

- `latest` is pending-key state, not an event log
- `queue` is in-memory event receive state
- ordering is runtime arrival order

The names may overlap, but the layers are different and must be documented separately.

## Backward Compatibility

Compatibility should be preserved.

Recommended rule:

- existing APIs keep their current meaning unless callers opt into the new receive-state model

In practice:

- do not silently change current transport behavior
- do not reinterpret existing callback APIs as implicit payload consumption
- add runtime receive-state support as explicit capability

This allows existing applications to continue working while new bindings and integrations adopt the normalized model.

## Relationship To Binding Design

This runtime receive model is a core concern, not a C#-only concern.

It affects:

- C++ API design
- C facade design
- Python bindings
- C# bindings
- future language bindings

Bindings should sit on top of this model rather than reinvent per-transport receive handling on their own.
