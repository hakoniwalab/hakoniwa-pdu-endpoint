# C# Engine Integration

This document describes the intended integration model for using the C# binding with game engines such as Unity and Godot.

The repository intentionally provides:

- the native core
- the C facade
- the C# binding
- small binding-level examples

It intentionally does not provide engine-specific adapter components.

## Why This Matters

This project is designed so that:

- runtime receive semantics are defined in the native endpoint layer
- the C# binding stays thin
- engine-specific lifecycle control remains in the application

That makes the same binding usable across:

- Unity
- Godot
- custom .NET hosts

without forcing one engine's lifecycle model onto the others.

## Integration Model

Use the C# binding directly from your engine-side code.

Recommended building blocks:

- `Endpoint`
  - thin direct binding over the native endpoint

Recommended runtime pattern:

1. create the endpoint during your engine-side startup
2. call `Open(...)`
3. call `Start(...)`
4. if the selected transport requires polling, call `ProcessRecvEvents()`
5. call `SetRecvEvent(...)` for keys you want tracked as pending events
6. consume pending receive state explicitly through `GetPendingCount()` and `RecvNext(...)`
7. call `Stop()` and `Close()` during shutdown

## Native Library Placement

The C# binding uses P/Invoke and requires the native `hakoniwa_pdu_endpoint` shared library to be discoverable at runtime.

Typical file names:

- macOS: `libhakoniwa_pdu_endpoint.dylib`
- Linux: `libhakoniwa_pdu_endpoint.so`
- Windows: `hakoniwa_pdu_endpoint.dll`

Build a shared library with:

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared --target hakoniwa_pdu_endpoint
```

On Windows:

```powershell
.\build-win.ps1 -BuildShared
```

Your engine-side project must ensure this native library is copied or loadable from the runtime search path.

## Managed Assembly Placement

The managed binding assembly is built from:

- `csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj`

Build with:

```bash
dotnet build csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj
```

The resulting managed DLL should be referenced or copied into your engine-side C# project according to that engine's normal assembly workflow.

## Unity Pattern

Suggested lifecycle mapping:

- initialization:
  - create `Endpoint`
  - `Open(...)`
  - `Start()`
- per frame:
  - `ProcessRecvEvents()`
  - `RecvNext(...)` or your own drain loop
- shutdown:
  - `Stop()`
  - `Close()`

Minimal sketch:

```csharp
using Hakoniwa.PduEndpoint;

public sealed class HakoniwaRuntime
{
    private Endpoint? _endpoint;
    private readonly byte[] _buffer = new byte[1024];

    public void Initialize(string configPath)
    {
        _endpoint = new Endpoint("unity_endpoint", EndpointDirection.InOut);
        _endpoint.Open(configPath);
        _endpoint.Start();
    }

    public void Tick()
    {
        if (_endpoint is null)
        {
            return;
        }

        _endpoint.ProcessRecvEvents();

        while (true)
        {
            try
            {
                var record = _endpoint.RecvNext(_buffer.Length);
                HandleRecord(record);
            }
            catch (EndpointException ex) when (ex.Error == HakoPduError.NoEntry)
            {
                break;
            }
        }
    }

    public void Shutdown()
    {
        _endpoint?.Stop();
        _endpoint?.Close();
    }

    private void HandleRecord(PduRecord record)
    {
        // Application-owned dispatch.
    }
}
```

## Godot Pattern

Suggested lifecycle mapping:

- initialization:
  - create `Endpoint`
  - `Open(...)`
  - `Start()`
- per frame:
  - `ProcessRecvEvents()`
  - `RecvNext(...)` or your own drain loop
- shutdown:
  - `Stop()`
  - `Close()`

Minimal sketch:

```csharp
using Godot;
using Hakoniwa.PduEndpoint;

public partial class HakoniwaRuntimeNode : Node
{
    private Endpoint? _endpoint;

    public override void _Ready()
    {
        _endpoint = new Endpoint("godot_endpoint", EndpointDirection.InOut);
        _endpoint.Open("path/to/endpoint.json");
        _endpoint.Start();
    }

    public override void _Process(double delta)
    {
        if (_endpoint is null)
        {
            return;
        }

        _endpoint.ProcessRecvEvents();

        while (true)
        {
            try
            {
                var record = _endpoint.RecvNext(1024);
                HandleRecord(record);
            }
            catch (EndpointException ex) when (ex.Error == HakoPduError.NoEntry)
            {
                break;
            }
        }
    }

    public override void _ExitTree()
    {
        _endpoint?.Stop();
        _endpoint?.Close();
    }

    private void HandleRecord(PduRecord record)
    {
        // Application-owned dispatch.
    }
}
```

## Transport Notes

Not every transport uses `ProcessRecvEvents()` in the same way.

- SHM poll:
  - requires explicit `ProcessRecvEvents()`
- callback/thread-driven transports:
  - may not require polling
  - pending receive state should still be consumed explicitly from upper-layer code

Calling `ProcessRecvEvents()` every frame is still a reasonable uniform integration pattern.

## Recommended Starting Point

If you are integrating into Unity or Godot, start from these repository assets:

- binding API:
  - `csharp/hakoniwa_pdu_endpoint/`
- examples:
  - `csharp/examples/MinimalExample/`
  - `csharp/examples/ManualPumpExample/`
  - `csharp/examples/RecvNextExample/`
- smoke tests:
  - `csharp/tests/SmokeTests/`

The examples show the binding usage.
The engine-side lifecycle connection should remain in your application code.
