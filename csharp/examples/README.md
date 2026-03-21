# C# Examples

These examples are intentionally small reference programs for the managed binding.
They are not Unity or Godot adapters.

Current examples:

- `MinimalExample`
  - minimal open/start/send/process/drain/stop flow
- `ManualPumpExample`
  - explicit per-frame style loop using `ProcessRecvEvents()` and `DrainPending()`
- `RecvNextExample`
  - runtime `recv_next(...)` for internal `latest` and `queue` cache semantics

## Build

Minimal example:

```bash
dotnet build csharp/examples/MinimalExample/MinimalExample.csproj
```

Manual pump example:

```bash
dotnet build csharp/examples/ManualPumpExample/ManualPumpExample.csproj
```

`recv_next` example:

```bash
dotnet build csharp/examples/RecvNextExample/RecvNextExample.csproj
```

## Runtime Requirement

Running these examples requires:

- the native `hakoniwa_pdu_endpoint` shared library
- a valid endpoint config path

The examples are designed to show managed-side usage patterns. They do not package the native shared library yet.

## Integration Pattern

For Unity/Godot style integration, the intended frame loop is:

1. `endpointAsync.ProcessRecvEvents()`
2. `endpointAsync.DrainPending()`

That keeps native receive progression separate from managed handler execution.

The repository intentionally does not ship engine-specific wrappers around this pattern.
Use these examples as the binding-level reference and connect them to your engine lifecycle on the application side.
