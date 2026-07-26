# Build Architecture

## Purpose

`hakoniwa-pdu-endpoint` has several independent build dimensions:

- native C++ only vs. Python (`cffi`) binding
- Hakoniwa Core integration for SHM/time-source support
- Zenoh transport
- MQTT transport
- tests, examples, tools, and benchmarks
- platform/toolchain details such as MSVC and vcpkg on Windows

Historically these dimensions were exposed directly through CMake flags and separate
`bash` / PowerShell helper scripts. That works for maintainers who know the dependency
graph, but it makes the user-facing build flow depend on OS-specific command knowledge.

The build architecture now treats these as two layers:

1. **intent manifest** — the user states the capabilities they want;
2. **configure/build tool** — resolves dependencies and translates the intent into CMake
   and Python binding steps for the current platform.

The source of truth for user intent is `hakoniwa-build.yaml`.

## Design principles

### 1. Features are independent axes

Python binding, Hakoniwa Core, Zenoh, and MQTT are not aliases for one another.

| Requested capability | Python/cffi | Hakoniwa Core | Zenoh | MQTT |
|---|---:|---:|---:|---:|
| C++ + TCP/UDP/WebSocket/Storage | no | no | no | no |
| Python + TCP/UDP/WebSocket/Storage | yes | no | no | no |
| C++ + SHM/time source | no | yes | no | no |
| Python + SHM/time source | yes | yes | no | no |
| Python + Zenoh | yes | no | yes | no |

The configurator derives implementation requirements from those independent choices.

### 2. The manifest describes intent, not CMake syntax

Do not copy CMake variable names into the user-facing manifest unless a capability cannot
be expressed otherwise. The user should say "Python binding", "Hakoniwa Core", or
"Zenoh", not remember a set of `-D...` switches.

### 3. OS-specific behavior stays below the resolver

The normal workflow is the same on Windows, macOS, and Linux:

```text
hakoniwa-build.yaml
        |
        v
tools/hako.py doctor
        |
        v
tools/hako.py configure
        |
        v
tools/hako.py build
        |
        v
tools/hako.py test
```

OS-specific logic is limited to toolchain and runtime discovery:

- Windows: MSVC/vswhere, optional vcpkg toolchain, DLL search directories
- macOS: dylib naming/search
- Linux: shared-object naming/search and native library architecture directories

Linux `aarch64` and `arm64` are normalized to the same `arm64` platform architecture by
the resolver. A missing prebuilt Linux ARM64 release bundle is a packaging limitation,
not a separate build model: source builds still use the same manifest/configure flow.
Architecture-specific system library fallbacks must use CMake platform information such
as `CMAKE_LIBRARY_ARCHITECTURE` rather than hard-coded x86_64 paths.

The build model itself does not change by OS.

### 4. Existing CMake behavior remains available

The configurator is an orchestration layer. It does not remove direct CMake use and does
not require an immediate rewrite of the existing `build*.bash` / `build*.ps1` helpers.
Those scripts remain compatibility/developer entry points while the manifest flow becomes
the recommended path.

### 5. Resolution is observable

`tools/hako.py` writes `.hako/resolved-build.yaml` and `.hako/cmake-args.txt`.
When a third party reports a build problem, these files make the resolved platform,
features, paths, and CMake options visible without reconstructing the user's shell state.

## Manifest v1

The initial manifest deliberately uses a small YAML subset: nested mappings and scalar
values only. This keeps the bootstrap tool dependency-free; no PyYAML installation is
required before configuration can be inspected.

Example:

```yaml
version: 1

build:
  type: Release
  dir: build
  shared: auto
  parallel: 0

bindings:
  python: true

features:
  hakoniwa_core: false
  zenoh: false
  mqtt: false

validation:
  tests: false
  examples: false
  tools: false
  benchmarks: false
  python_import: true

paths:
  hakoniwa_core_root: ""
  vcpkg_root: ""
```

The default path intentionally builds only the native shared library plus the Python
binding and enables the import smoke. Tests, examples, tools, and benchmarks are explicit
opt-ins so an unrelated CMake default cannot silently enlarge the user-requested build.

### `build.shared`

`auto` means:

- `ON` when `bindings.python: true`, because the cffi extension loads the native shared
  library;
- `OFF` when `bindings.python: false`.

A Python binding with `build.shared: false` is rejected during configuration rather than
failing later at import time.

### `features.hakoniwa_core`

This is `false` in the manifest by default on every OS. It is enabled only when the user
needs SHM or Hakoniwa time-source integration.

This intentionally differs from the legacy raw-CMake default on macOS/Linux. The
configurator always passes an explicit ON/OFF value, so the manifest path is deterministic
without changing legacy CMake callers.

When enabled, the root is resolved from, in order:

1. `paths.hakoniwa_core_root`
2. `HAKONIWA_CORE_ROOT`
3. `HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT`

### `features.zenoh` and `features.mqtt`

These map to the corresponding optional native transports. They are independent from
Python and Hakoniwa Core.

`features.zenoh: true` also introduces a Rust toolchain requirement because the vendored
`zenoh-c` implementation is built from Rust. `tools/hako.py doctor` therefore checks that
both `cargo` and `rustc` are available before build. Direct-CMake users receive the same
preflight during CMake configure.

The Zenoh dependency is treated as an implementation detail of pdu-endpoint. When the
endpoint itself is built as a shared library (including the Python/cffi flow), the build
prefers the vendored `zenohc::static` target rather than inheriting `BUILD_SHARED_LIBS`
through `zenohc::lib`. This avoids a runtime `libzenohc.so` dependency that can collide
with another Zenoh runtime in the same process, notably the vendor library used by ROS 2
`rmw_zenoh_cpp`.

### Python binding

`bindings.python: true` is the default manifest behavior. The configurator:

1. requires a shared native library;
2. checks `cffi` and `setuptools` before build;
3. builds the native library;
4. builds `_c_endpoint_ffi`;
5. prepares runtime search directories;
6. can run an import smoke test.

On Windows, `vswhere.exe` is discovered from PATH or the normal Visual Studio Installer
location and its directory is added to the child build environment. This removes the need
for a contributor to manually reconstruct the Visual Studio discovery setup just to build
cffi.

## Runtime DLL directories

For Windows Python imports, dependent DLLs can live outside the pdu-endpoint build output,
for example in Hakoniwa Core or vcpkg directories. The build tool exports these directories
through:

```text
HAKO_PDU_ENDPOINT_RUNTIME_DIRS
```

The value is an OS-path-separator-delimited list. The Python package registers these
locations with `os.add_dll_directory()` before the cffi extension is loaded.

The configurator derives runtime directories from the resolved native library, Hakoniwa
Core root, and vcpkg root. Manual consumers can also set the variable directly.

## Commands

Inspect prerequisites and the resolved build without compiling:

```bash
python tools/hako.py doctor
python tools/hako.py configure --dry-run
```

Configure and build:

```bash
python tools/hako.py configure
python tools/hako.py build
```

Run configured validation:

```bash
python tools/hako.py test
```

Use an alternative manifest:

```bash
python tools/hako.py build --config path/to/my-build.yaml
```

## Migration strategy

Phase 1 (this architecture):

- introduce `hakoniwa-build.yaml` and `tools/hako.py`;
- make Python binding a first-class resolved capability;
- add doctor/preflight diagnostics;
- make Windows runtime DLL paths explicit and reproducible;
- keep existing scripts working.

Phase 2:

- move CI Python build jobs to the manifest flow on all supported OSes;
- reduce duplicated feature-resolution logic in shell/PowerShell helpers;
- add more executable validation for feature combinations, including shared Python/Zenoh linkage.

Phase 3:

- consider packaging `hako` as a standalone developer tool if requiring a Python
  interpreter for orchestration becomes undesirable for C++-only users;
- consider generating CMake presets from the resolved model once the manifest schema is
  stable.

## Maintainer rule

When a new optional transport, binding, or runtime capability is added, first decide which
independent manifest axis owns it. Do not encode dependency combinations directly into
OS-specific scripts. Add the dependency rule to the resolver, make it observable in
`resolved-build.yaml`, and add a smoke test for the resulting capability combination.
