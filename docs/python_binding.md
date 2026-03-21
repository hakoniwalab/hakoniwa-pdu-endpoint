# Python Binding Design

## Goal

Provide Python runtime access to `hakoniwa-pdu-endpoint` without embedding Python into the core C++ library.

The Python binding follows a layered design:

- stable native boundary at the C facade
- thin Python wrapper over the C facade
- optional callback convenience inside the thin binding

This keeps the core library language-neutral and makes callback behavior explicit.

## Layering

Current structure:

1. `c_endpoint.h` / `c_endpoint.cpp`
   - portable C ABI over `hakoniwa::pdu::Endpoint`
2. `python/hakoniwa_pdu_endpoint/c_endpoint.py`
   - thin `cffi` wrapper over the C ABI, plus optional callback convenience
3. `python/hakoniwa_pdu_endpoint/endpoint_container.py`
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

It should not add hidden threading or scheduling policy, and it should not redefine receive semantics independently of the native layer.

## Callback Convenience Responsibilities

`c_endpoint.py` also provides optional callback convenience helpers.

Their responsibilities are:

- offer callback-style handling on top of the thin wrapper
- avoid forcing user Python logic onto transport-facing callback threads

These helpers are convenience features, not the canonical definition of runtime receive behavior.

## Standard Receive Model

The standard receive model should be defined by the native runtime and exposed through the C facade.

Preferred model:

- native/C layer owns pending receive state
- Python consumes pending events through explicit APIs such as `recv(...)` and `recv_next(...)`
- upper-layer Python code decides whether and when to dispatch user handlers
- explicit event registration in the C facade is optional and should not be required for plain receive calls

Direct native callback handling may remain available for compatibility, but it should not be the long-term primary receive model for all transports.

## Dispatch Model

If callback-style convenience is desired:

- a higher-level wrapper may build queueing/dispatch behavior on top of explicit pulls
- or a compatibility callback wrapper may enqueue work and dispatch later
- user Python handlers should still run from caller-controlled code paths where practical

This keeps Python behavior explicit and transport-independent.

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

- transport/native callback threads may cause native receive state to become pending
- Python code consumes that state through explicit API calls
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
- native/C layer should define runtime receive semantics
- async callback handling, when used, remains an optional convenience layer above the thin binding

This preserves explicit ownership, explicit threading, and explicit runtime behavior.
