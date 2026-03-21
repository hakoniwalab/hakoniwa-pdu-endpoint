# Python Binding Design

## Goal

Provide Python runtime access to `hakoniwa-pdu-endpoint` without embedding Python into the core C++ library.

The Python binding follows a layered design:

- stable native boundary at the C facade
- thin Python wrapper over the C facade
- higher-level async wrapper above the thin binding

This keeps the core library language-neutral and makes callback behavior explicit.

## Layering

Current structure:

1. `c_endpoint.h` / `c_endpoint.cpp`
   - portable C ABI over `hakoniwa::pdu::Endpoint`
2. `python/hakoniwa_pdu_endpoint/c_endpoint.py`
   - thin `cffi` wrapper over the C ABI
3. `python/hakoniwa_pdu_endpoint/c_endpoint_async.py`
   - async callback wrapper built on top of the thin Python layer
4. `python/hakoniwa_pdu_endpoint/endpoint_container.py`
   - pure-Python container orchestration by composing wrapped endpoints

This layering is intentional. Python does not reach directly into the C++ API.

## Why the C Facade

The Python binding uses the C facade as the ABI boundary because it provides:

- a stable language-neutral surface
- opaque handle ownership
- caller-owned buffers for receive operations
- callback support without linking Python-specific logic into the C++ core

This allows Python integration to evolve independently of the native implementation details.

## Thin Wrapper Responsibilities

`c_endpoint.py` is intentionally thin.

Its responsibilities are:

- load the generated `cffi` module
- translate Python values to C structs
- call the C facade
- raise Python exceptions from native error codes
- keep callback objects alive on the Python side

It should not add hidden threading or scheduling policy.

## Async Wrapper Responsibilities

`c_endpoint_async.py` provides the higher-level callback model.

Its responsibilities are:

- subscribe native receive callbacks through the thin wrapper
- copy callback payload bytes immediately
- enqueue copied events in a Python-owned queue
- dispatch handlers later on a Python-owned thread

This avoids running user Python logic directly on transport-facing callback threads.

## Callback Model

The underlying native callback should do the minimum possible work:

- capture the resolved key
- copy payload bytes into Python-managed memory
- enqueue an event
- return immediately

The native callback must not rely on the payload pointer remaining valid after callback return.
That pointer is borrowed only for the duration of the callback.

## Dispatch Model

Python event handlers are not executed directly inside the native callback path.

Instead:

- the low-level callback copies and enqueues
- a Python-owned dispatch thread reads queued events
- registered Python handlers are invoked later from that thread

This design is safer for callback-heavy usage because it keeps Python execution away from transport-facing timing constraints.

## Current Event Types

The Python side currently models:

- `PduResolvedKey`
- `PduKey`
- `PduRecord`
- `PduEvent`

This preserves the same distinction as the C++ core:

- resolved-key operations always work
- name-based helpers depend on loaded `pdu_def`
- queue-style receive is explicit through `recv_next`

## EndpointContainer In Python

`endpoint_container.py` is implemented directly in Python rather than through a new native facade.

Reason:

- the container is mostly configuration and lifecycle orchestration
- Python can reproduce that logic cleanly by composing wrapped `Endpoint` instances
- no extra native ABI surface is required for the current scope

Current responsibilities:

- load the container config
- select entries by `nodeId`
- resolve relative config paths
- initialize endpoints
- manage lifecycle across multiple endpoints

## Threading Model

The intended Python threading model is:

- transport/native callback threads may trigger event capture
- Python callback code is deferred to a Python-owned dispatch thread
- the application remains responsible for endpoint lifecycle and any explicit poll calls such as `process_recv_events()`

This matches the broader design philosophy of explicit integration control.

## Non-Goals For Current Python Support

- no direct `Python.h` embedding in the core C++ library
- no Python-specific behavior inside the native endpoint implementation
- no exposure of every native orchestration helper through the C ABI
- no hidden semantic changes relative to the C++ API

## Design Summary

The Python binding is intentionally conservative:

- native core stays language-neutral
- C facade defines the ABI boundary
- `cffi` provides a thin binding
- async callback handling is implemented one layer above the thin binding

This preserves explicit ownership, explicit threading, and explicit runtime behavior.
