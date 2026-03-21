# C# Binding

This directory contains the initial C# binding scaffold for `hakoniwa-pdu-endpoint`.

Current layout:

- `hakoniwa_pdu_endpoint/`
  - managed binding library
- `examples/`
  - small reference programs

## Design

The binding follows the same boundary as the Python integration:

- C++ core
- C facade
- thin managed wrapper
- async queue layer above the thin wrapper

See:

- `docs/csharp_binding.md`
- `docs/csharp_engine_integration.md`
- `docs/python_binding.md`

## Current Scope

The managed side currently provides:

- native handle ownership via `SafeHandle`
- thin `Endpoint` wrapper over the C facade
- `EndpointAsync` queue-based event capture and explicit `DrainPending()`
- simple smoke-test coverage under `csharp/tests/`

The default callback model is:

- native callback copies payload into managed memory
- managed event is queued immediately
- user handlers run only when `DrainPending()` is called

This is the intended safe default for Unity and Godot style main-thread integration.

## Scope Boundary

This repository provides:

- the native core
- the C facade
- a minimal C# binding
- small executable examples

This repository does not provide:

- Unity-specific `MonoBehaviour` adapters
- Godot-specific `Node` adapters
- engine-version-specific packaging glue

That boundary is intentional.
The goal is to expose `Endpoint` and `EndpointAsync` cleanly and let engine-side projects decide how they connect those APIs to their own lifecycle.

## Native Library Requirement

The C# layer uses P/Invoke and expects a loadable native library named
`hakoniwa_pdu_endpoint`.

The current repository build mainly produces a static library for C++ linking.
To run the C# binding, a shared library packaging step is still required for each target platform.

Typical examples:

- macOS: `libhakoniwa_pdu_endpoint.dylib`
- Linux: `libhakoniwa_pdu_endpoint.so`
- Windows: `hakoniwa_pdu_endpoint.dll`

## Projects

Library:

- `csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj`

Example:

- `csharp/examples/MinimalExample/MinimalExample.csproj`
- `csharp/examples/ManualPumpExample/ManualPumpExample.csproj`
- `csharp/examples/RecvNextExample/RecvNextExample.csproj`

Smoke tests:

- `csharp/tests/SmokeTests/SmokeTests.csproj`

## Build

Library:

```bash
dotnet build csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj
```

Example:

```bash
dotnet build csharp/examples/MinimalExample/MinimalExample.csproj
```

Smoke tests:

```bash
bash test-csharp.bash
```

Running the example also requires the native shared library to be discoverable by the runtime.

## Engine Integration

For Unity or Godot integration, the expected usage is to keep engine-specific lifecycle code on the application side and call the binding directly.

Typical pattern:

1. hold `Endpoint` or `EndpointAsync` in your engine-side object
2. initialize/open/start during your engine's startup lifecycle
3. call `ProcessRecvEvents()` if the selected transport requires polling
4. call `DrainPending()` from the engine main thread when using `EndpointAsync`
5. stop/close during shutdown

The repository examples intentionally stop at this boundary.

For Unity/Godot-oriented setup guidance, see:

- `docs/csharp_engine_integration.md`
