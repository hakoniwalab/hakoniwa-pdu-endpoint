# SHM callback module boundaries

This document describes the runtime memory model behind Hakoniwa Shared Memory (SHM) callback endpoints, with emphasis on the difference between Windows and Linux/macOS linking layouts.

The central rule is simple:

> The OS-owned shared-memory region and the module-local Core/Asset state that points into that region are different things.

A process can therefore have valid Hakoniwa shared memory while one DLL-local Core instance still has an uninitialized pointer such as `recv_event_table_ == nullptr`.

## 1. Two layers of state

Hakoniwa SHM callback integrations have two distinct layers.

```text
Process-local / module-local state

  HakoProData instance
    ├─ master_data_ptr
    ├─ pro_data_ptr
    ├─ asset_ptr
    └─ recv_event_table_ ───────────────┐
                                         │
                                         v
                              +-----------------------+
                              | OS shared memory      |
                              |                       |
                              | master data           |
                              | PDU data              |
                              | recv-event table      |
                              | service table         |
                              +-----------------------+
```

The shared-memory region is owned by the OS and can be mapped by multiple modules or processes.

The pointers and singleton-like objects used to access that region are ordinary process/module state. They must be initialized in the module that uses them.

This distinction matters most when the same executable process contains more than one binary module that embeds Hakoniwa Core/Asset implementation code.

## 2. Windows layout

On Windows, the callback `assets` target and `conductor` target are built as static libraries. The Core `hako` target is also static. A consumer executable and an Endpoint DLL can therefore each contain their own copies of Core/Asset implementation state.

A representative layout is:

```text
Windows process
│
├─ robot-arm-hakoniwa-asset.exe
│   ├─ assets.lib      ─┐
│   ├─ conductor.lib   │ static link
│   └─ hako.lib        ┘
│
│   Core / Asset module-local state A
│     ├─ asset_ptr
│     ├─ pro_data_ptr
│     ├─ master_data_ptr
│     └─ recv_event_table_ ─────────────┐
│                                       │
├─ hakoniwa_pdu_endpoint_core_callback.dll
│   ├─ assets.lib      ─┐
│   └─ hako.lib        ┘ static/link dependency
│
│   Core / Asset module-local state B
│     ├─ asset_ptr
│     ├─ pro_data_ptr
│     ├─ master_data_ptr
│     └─ recv_event_table_ ─────────────┤
│                                       │
└───────────────────────────────────────┼─────────
                                        v
                             +-----------------------+
                             | Windows shared memory |
                             | master / PDU / events |
                             +-----------------------+
```

The important consequence is:

```text
same process != same Core static state
```

The shared-memory objects may already exist and be valid, while the Endpoint DLL's local `asset_ptr`, `pro_data_ptr`, or `recv_event_table_` is still uninitialized.

That module must attach its own local Core/Asset state to the already-existing shared memory before it performs callback SHM I/O or receive-event registration.

## 3. Linux/macOS layout

The non-Windows callback `assets` and `conductor` targets are built as shared libraries. This normally places the callback frontend implementation in shared objects used by downstream consumers instead of embedding a separate copy directly into each consumer binary.

```text
Linux / macOS process
│
├─ application executable
│       │
│       ├──────────────┐
│       v              v
│  libassets.so    libconductor.so
│  (or .dylib)     (or .dylib)
│       │
│       │ callback/Core frontend state
│       │
├─ libhakoniwa_pdu_endpoint_core_callback.so
│  (or .dylib)
│       │
│       └── depends on the callback/assets frontend
│
└───────────────────────────────┐
                                v
                     +-----------------------+
                     | OS shared memory/mmap |
                     | master / PDU / events |
                     +-----------------------+
```

This link topology makes duplicate callback frontend state less likely than on Windows, because consumers normally resolve the shared `assets` frontend instead of embedding `assets.lib` into each binary module.

Do not generalize this into "Linux shares all static state". The Core contains static-library pieces as well, and future link layouts may change. The portable rule is still:

> Never infer Core/Asset initialization from process identity alone. A module that performs SHM callback operations must have a valid local attach context.

The Windows layout simply makes this requirement visible more readily.

## 4. Why `recv_event_table_` can be null while SHM exists

The physical shared memory and the pointer that references its receive-event table have different lifecycles.

```text
Module A

  HakoProData A
    recv_event_table_ ───────────────┐
                                      │
                                      v
                           +---------------------+
                           | shared event table  |
                           +---------------------+
                                      ^
                                      │
Module B                              │
                                     │
  HakoProData B                       │
    recv_event_table_ = nullptr       │
```

Module B does not become ready merely because Module A already mapped the region. Module B must run the attach/load path that initializes its own pointer state.

This is why an error such as:

```text
ERROR: recv_event_table_ is null
```

should be interpreted as a local Core/Asset attach-state problem first, not as proof that the OS shared-memory object itself does not exist.

## 5. Endpoint asset context

`Endpoint::open()` has two intentional meanings.

### External endpoint

```cpp
endpoint.open(config_path);
```

This selects an external-use context. The SHM callback backend eventually attaches through:

```text
hako_asset_attach_core()
```

Use this when the Endpoint is an external peer and is not operating as a registered Hakoniwa asset.

### Endpoint owned by a Hakoniwa asset

```cpp
endpoint.open(config_path, asset_name);
```

This passes the asset identity into the communication layer. The SHM callback backend stores the context and lazily attaches through:

```text
hako_asset_attach_core_with_name(asset_name, pdu_config_path)
```

when SHM access or receive-event registration first requires Core access.

`asset_name` does **not** register an asset. It specifies which already-defined Hakoniwa asset context the Endpoint should use for PDU I/O.

Therefore a runtime that later registers itself as `Nova5` should open its callback SHM Endpoint with the same asset name:

```cpp
endpoint.open(endpoint_config_path, "Nova5");
```

Using the one-argument overload in that situation discards information the callback backend needs in module-separated environments such as Windows.

## 6. Initialization sequence

A typical asset-owned callback Endpoint follows this sequence:

```text
Endpoint::open(config, asset_name)
    |
    | stores asset context
    v
application registers/initializes the Hakoniwa asset
    |
    v
Endpoint::start()
    |
    v
Endpoint::post_start()
    |
    v
SHM callback operation / recv-event registration
    |
    v
ensure_attached()
    |
    v
hako_asset_attach_core_with_name(asset_name, pdu_config_path)
    |
    v
this module's Core/Asset state is attached to existing SHM
```

The attach is lazy. Passing `asset_name` to `open()` does not itself register the asset and does not force immediate SHM access.

## 7. Build requirement

Asset context and SHM capability are separate concerns.

The Endpoint package must first be built with Hakoniwa Core support enabled:

```yaml
features:
  hakoniwa_core: true
```

Then the consumer chooses the callback frontend explicitly:

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_callback
)
```

For an asset-owned SHM callback Endpoint, both conditions matter:

```text
build-time:
  Hakoniwa Core / SHM capability exists

runtime:
  Endpoint is opened with the correct asset context
```

## 8. Historical rationale

The explicit asset-context attach path was introduced to handle module-boundary behavior observed on Windows.

Relevant history:

- `hakoniwalab/hakoniwa-core-pro#64` — explicit attach initialization for external/DLL consumers
- `hakoniwalab/hakoniwa-pdu-endpoint#31` — `Endpoint::open(config_path, asset_name)` / `open_with_asset`
- `hakoniwalab/hakoniwa-core-pro#90` — a later Windows callback failure that exposed a caller using the external-context overload inside an asset runtime
- `hakoniwalab/hakoniwa-robot-runtime#7` — runtime-side correction to pass the asset name into `Endpoint::open`

The design lesson is broader than any one issue:

> Shared memory is shared storage. Core/Asset runtime state is not automatically shared across binary-module boundaries. Make the ownership context explicit at the Endpoint boundary.

## 9. Review checklist for SHM callback consumers

When adding a new SHM callback consumer, verify all of the following:

- Hakoniwa Core support is enabled in the Endpoint build.
- The consumer links `hakoniwa_pdu_endpoint::core_callback` rather than reconstructing Core libraries manually.
- External peers use `Endpoint::open(config_path)` intentionally.
- Registered Hakoniwa assets use `Endpoint::open(config_path, asset_name)` with the same asset identity used by the asset runtime.
- The Endpoint config contains a resolvable `pdu_def_path` when asset-context attach is required.
- Windows testing covers `post_start()` and receive-event registration, not only Endpoint construction/open.
- A successful SHM creation in another module is not treated as evidence that the current module's callback state is initialized.
