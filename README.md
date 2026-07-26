[![CI](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/ci.yml/badge.svg)](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/ci.yml)
[![Core Variants](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/core-variants.yml/badge.svg)](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/core-variants.yml)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/hakoniwalab/hakoniwa-pdu-endpoint)

# hakoniwa-pdu-endpoint

[English](README.md) | [日本語](README.ja.md)

`hakoniwa-pdu-endpoint` is the Endpoint infrastructure used by Hakoniwa distributed simulations.

It is more than a transport wrapper. Endpoint separates three semantic concerns:

- `cache`: lifetime, overwrite, and queueing semantics
- `comm`: delivery, persistence, and transport semantics
- `pdu_def`: optional meaning for PDU names, channel IDs, and sizes

This separation lets applications change transport or storage behavior without changing the Endpoint API.

For the design rationale, see [docs/design_philosophy.md](docs/design_philosophy.md).

## Supported capabilities

- TCP / UDP / WebSocket
- Hakoniwa Shared Memory
- Storage
- Zenoh
- MQTT
- `latest` / `queue` caches
- PDU name resolution
- C facade
- Python/cffi binding
- C# binding

For TCP vs. SHM(callback) performance characteristics, see [benchmarks/PERFORMANCE.md](benchmarks/PERFORMANCE.md).

## CI and supported platforms

Two workflows cover different contracts:

- `ci`: normal builds, tests, bindings, manifest resolution, and transport combinations
- `core-variants`: Hakoniwa Core package integration and external CMake consumer linkage

`core-variants` continuously validates this flow on Ubuntu x64, macOS, and Windows x64:

```text
build/install hakoniwa-core-pro
        ↓
build/install hakoniwa-pdu-endpoint with Core enabled
        ↓
configure a separate consumer project
        ↓
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)
        ↓
link core_callback and core_polling consumers
```

Linux ARM64 (`aarch64`) uses the same source-build and manifest model. Native ARM64 CI is being added to the `core-variants` workflow; until that job is confirmed, this README does not claim ARM64 as CI-verified.

Documentation-only changes do not trigger the heavy build workflows.

## Recommended build flow: manifest driven

The recommended developer workflow is the same on Windows, macOS, and Linux.

Declare the capabilities you need in `hakoniwa-build.yaml`, then let `tools/hako.py` resolve platform-specific CMake arguments and prerequisites.

Default manifest:

```yaml
bindings:
  python: true

features:
  hakoniwa_core: false
  zenoh: false
  mqtt: false
```

Typical changes:

- C++ only: `bindings.python: false`
- SHM / Hakoniwa time source: `features.hakoniwa_core: true`
- Zenoh: `features.zenoh: true`
- MQTT: `features.mqtt: true`

Python/cffi is a language binding and is independent from Hakoniwa Core. TCP, UDP, WebSocket, Storage, Zenoh, and MQTT can be used without Core.

Inspect and build:

```bash
python tools/hako.py doctor
python tools/hako.py configure --dry-run
python tools/hako.py build
python tools/hako.py test
```

The resolver writes:

```text
.hako/resolved-build.yaml
.hako/cmake-args.txt
```

Include these files when reporting build problems so the resolved feature set and CMake inputs are reproducible.

For the full model, see [docs/build-architecture.md](docs/build-architecture.md).

## Hakoniwa Core artifacts

`features.hakoniwa_core` is a **build capability switch**, not a callback/polling selector.

The manifest decides which artifacts exist. The downstream CMake target decides which Hakoniwa Core frontend the application depends on.

### Core disabled

```yaml
features:
  hakoniwa_core: false
```

Primary build target:

```text
hakoniwa_pdu_endpoint
```

Installed CMake target:

```cmake
hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint
```

Use this for Core-free TCP / UDP / WebSocket / Storage / Zenoh / MQTT applications.

### Core enabled

```yaml
features:
  hakoniwa_core: true
```

Three native targets are generated:

| Build target | Installed CMake target | Core dependency | Intended use |
|---|---|---|---|
| `hakoniwa_pdu_endpoint` | `hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint` | callback + polling | legacy compatibility |
| `hakoniwa_pdu_endpoint_core_callback` | `hakoniwa_pdu_endpoint::core_callback` | `hakoniwa-core::assets` | preferred for new callback / asset integrations |
| `hakoniwa_pdu_endpoint_core_polling` | `hakoniwa_pdu_endpoint::core_polling` | `hakoniwa-core::shakoc` | preferred for new polling integrations |

The legacy `hakoniwa_pdu_endpoint` target intentionally remains compatible with existing applications. When Core is enabled, it contains both callback and polling SHM/time-source implementations.

For new Core-aware applications, prefer an explicit variant:

```text
Core-free application
  -> hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint

Hakoniwa callback/assets integration
  -> hakoniwa_pdu_endpoint::core_callback

Hakoniwa polling/shakoc integration
  -> hakoniwa_pdu_endpoint::core_polling
```

The distinction is deliberate:

```text
manifest
  decides which capabilities and artifacts are built

consumer CMake target
  decides which Core frontend is linked
```

There is therefore no callback/polling selector in the manifest.

## Consuming the installed CMake package

Consumers should use exported CMake targets rather than reconstructing include and library paths manually.

Callback/assets integration:

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_callback
)
```

Polling/shakoc integration:

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_polling
)
```

A Core-enabled Endpoint package resolves `hakoniwa-core` transitively. Consumers should not need to `find_library()` `assets`, `shakoc`, or `hako`, nor hard-code the Core install layout.

The `core-variants` workflow validates this installed-package contract with a separate external consumer project.

## Requirements

- C++20 compiler
- CMake 3.16 or newer
- Boost headers
- GoogleTest when building tests
- Hakoniwa Core when using SHM or Hakoniwa time sources
- Python 3 + `cffi` / `setuptools` when `bindings.python: true`

On Windows, Boost.Asio / Boost.Beast through vcpkg is the recommended setup.

## Direct CMake build

Direct CMake remains available as a compatibility and advanced-developer path.

```bash
cmake -S . -B build
cmake --build build
```

Core-enabled example:

```bash
cmake -S . -B build-core \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=ON \
  -DHAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT=/path/to/hakoniwa-core-install
cmake --build build-core
```

The legacy raw-CMake defaults may differ from the manifest defaults. Use the manifest flow when reproducibility is more important than preserving historical build behavior.

## Endpoint configuration

An Endpoint configuration is composed of up to four parts:

- Endpoint config
- Cache config
- Communication (`comm`) config
- optional PDU Definition (`pdu_def`) config

Relative paths are resolved from the file that declares them.

Schemas are under `config/schema/`.

A typical endpoint config:

```json
{
  "name": "my_endpoint",
  "pdu_def_path": "comm/hakoniwa/pdudef.json",
  "cache": "cache/queue.json",
  "comm": "comm/hakoniwa/shm_comm.json"
}
```

An internal cache-only Endpoint uses:

```json
{
  "name": "my_internal_buffer",
  "cache": "cache/buffer.json",
  "comm": null
}
```

For configuration semantics and validation, see:

- [docs/tutorials/endpoint.md](docs/tutorials/endpoint.md)
- [docs/receive_semantics.md](docs/receive_semantics.md)
- [docs/storage_comm.md](docs/storage_comm.md)
- `config/schema/`

## Transport notes

### Storage

- `latest`: one current value per key; primary API is `recv(key, ...)`
- `queue`: append/receive-order semantics; primary API is `recv_next(...)`

Use `hako_pdu_storage_debug` to inspect generated storage files.

### Zenoh

Enable with:

```yaml
features:
  zenoh: true
```

Zenoh is modeled as a first-class pub/sub transport. Native Zenoh topology stays in its Zenoh config; Hakoniwa Endpoint semantics remain in the Endpoint comm config.

### MQTT

Enable with:

```yaml
features:
  mqtt: true
```

MQTT is modeled as broker-based pub/sub while preserving the same Endpoint API.

### Shared Memory

SHM requires Hakoniwa Core. Runtime configuration can still select callback or polling behavior for the legacy target, but new CMake consumers should normally choose the corresponding explicit build variant described above.

## Language bindings

### Python

The Python binding is layered over the C facade with `cffi`.

Main modules:

- `python/hakoniwa_pdu_endpoint/c_endpoint.py`
- `python/hakoniwa_pdu_endpoint/endpoint_container.py`

See [docs/python_binding.md](docs/python_binding.md).

### C#

The C# binding also uses the C facade as the native ABI boundary.

See:

- [docs/csharp_binding.md](docs/csharp_binding.md)
- [docs/csharp_engine_integration.md](docs/csharp_engine_integration.md)

## Examples and tools

Runnable examples are under `examples/` and `python/examples/`.

Useful entry points:

- [examples/README.md](examples/README.md)
- [benchmarks/README.md](benchmarks/README.md)
- [docs/diagrams/README.md](docs/diagrams/README.md)
- [FAQ.md](FAQ.md)

## Maintainer rule

When adding a new optional transport, binding, or runtime capability:

1. decide which independent manifest axis owns it;
2. keep OS-specific resolution below the configurator;
3. expose dependencies through CMake targets, not absolute include/library paths;
4. add a smoke test for the resulting capability combination;
5. validate installed-package consumption when the capability is public to downstream CMake users.
