# Windows Support Issue

## Goal

Make `hakoniwa-pdu-endpoint` buildable and usable on Windows without weakening
the existing Linux/macOS paths.

This is not only about making CMake compile once. It also includes:

- runtime portability
- optional transport dependencies
- C facade usability
- Python `cffi` usability
- install and CI strategy

## Immediate Goal

The first goal is not "Windows support complete".

The first goal is:

- cut a socket portability interface
- move current POSIX behavior behind that interface
- prove no regression on existing POSIX environments

Only after that should Windows-specific implementation begin.

## Current Status

The repository currently works primarily with Unix-like assumptions.

Confirmed recent areas that would be affected by Windows support:

- core C++ runtime
- TCP / UDP / TCP mux transports
- Storage / Zenoh / MQTT build integration
- C facade
- Python `cffi` wrapper
- install scripts and README instructions

## First Pass: Incompatibility Inventory

### 1. POSIX Socket Headers and Functions

These are the most obvious portability blockers.

Observed in the codebase:

- `src/comm_tcp.cpp`
  - `<unistd.h>`
  - `<fcntl.h>`
  - `<arpa/inet.h>`
  - `<sys/socket.h>`
  - `::close`
  - `::poll`
- `src/comm_tcp_mux.cpp`
  - `<arpa/inet.h>`
  - `<fcntl.h>`
  - `<sys/socket.h>`
  - `<unistd.h>`
  - `::close`
- `src/comm_udp.cpp`
  - `<unistd.h>`
  - `<fcntl.h>`
  - `<arpa/inet.h>`
  - `<sys/socket.h>`
  - `::close`
  - `inet_pton`
- `src/socket_utils.cpp`
  - `<arpa/inet.h>`
  - `inet_pton`
- headers
  - `include/hakoniwa/pdu/socket_utils.hpp` includes `<netdb.h>`
  - `include/hakoniwa/pdu/comm/comm_tcp.hpp` includes `<netinet/in.h>`
  - `include/hakoniwa/pdu/comm/comm_udp.hpp` includes `<netinet/in.h>`
  - `include/hakoniwa/pdu/comm/packet.hpp` includes `<arpa/inet.h>`

Windows impact:

- these headers do not exist as-is on MSVC
- socket lifetime uses `closesocket`, not `close`
- Winsock initialization (`WSAStartup`) is required
- error mapping uses `WSAGetLastError`, not `errno`
- polling APIs differ

### 2. Install Prefix and Unix-Centric Runtime Layout

The repository assumes `/usr/local/hakoniwa`.

Observed in:

- `README.md`
- `src/CMakeLists.txt`
- GitHub workflows
- Python wrapper preload logic

Examples:

- headers under `/usr/local/hakoniwa/include`
- libraries under `/usr/local/hakoniwa/lib`
- `LD_LIBRARY_PATH` assumptions in CI
- shell scripts:
  - `build.bash`
  - `install.bash`
  - `uninstall.bash`

Windows impact:

- hard-coded Unix paths must become configurable
- shell-script install flow is not enough
- DLL / import library layout must be considered

### 3. Python Wrapper Loader Logic Is macOS-Specific

Observed in:

- `python/hakoniwa_pdu_endpoint/c_endpoint.py`

Current behavior:

- preloads:
  - `libconductor.dylib`
  - `libassets.dylib`
  - `libshakoc.dylib`
- uses `ctypes.CDLL(..., mode=ctypes.RTLD_GLOBAL)`
- assumes `/usr/local/hakoniwa/lib`

Windows impact:

- `.dylib` names become `.dll`
- `RTLD_GLOBAL` path does not translate directly
- dynamic loader search behavior differs
- Python may need `os.add_dll_directory(...)`

### 4. Generated `cffi` Extension Build Assumptions

Observed in:

- `python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py`

Current assumptions:

- links against library names and directories known from Unix-like builds
- expects the core build under `build/src`
- uses runtime library directory settings appropriate for ELF / Mach-O flows

Windows impact:

- import library / DLL naming differs
- library search and runtime search differ
- the generated extension will need MSVC-compatible linking assumptions

### 5. CI and Tooling Are Unix-Oriented

Observed in:

- `.github/workflows/ci.yml`
- `.github/workflows/ci-zenoh.yml`
- `.github/workflows/ci-mqtt.yml`

Current assumptions:

- `bash build.bash`
- `sudo bash install.bash`
- Linux/macOS package managers
- `LD_LIBRARY_PATH`

Windows impact:

- a separate Windows workflow is needed
- PowerShell or pure CMake install flow is preferable
- dependency installation story must be explicit

### 6. Optional Transport Dependencies Need Separate Validation

#### Zenoh

Expected status:

- `zenoh-c` itself can support Windows
- but CMake fetch/build and CI reliability must be revalidated on Windows

#### MQTT

Expected status:

- Paho MQTT C / C++ should be feasible on Windows
- but broker-based integration tests and FetchContent configuration need proof

#### SHM

Likely highest-risk transport for Windows.

Reason:

- it depends on Hakoniwa-side facilities outside the normal socket stack
- the install/runtime model may differ substantially from Linux/macOS

### 7. C Facade Export/Import Strategy Is Not Frozen

Current C facade is public, but Windows usually needs explicit symbol export
discipline.

Likely need:

- export macro for the C facade
- shared-library strategy if DLL consumption matters
- verification that static-library-only use is enough for v1

### 8. README / Examples / User Guidance Are Not Windows-Ready

Observed issues:

- commands use `bash`
- install paths are Unix-specific
- Python section assumes a Unix-like layout
- MQTT examples assume `mosquitto` command directly in PATH

This is not a code blocker, but it is a real usability blocker.

## Recommended Prioritization

### Phase 1: Interface Extraction

Introduce a socket portability layer with:

- shared interface/header
- POSIX implementation
- Windows implementation stub or placeholder

The key point is to remove OS conditionals from transport implementations
themselves.

First-pass target:

- no behavior change
- no Windows functionality promise yet
- existing POSIX behavior preserved

### Phase 2: POSIX Regression Check

After transport code is rewritten to use the portability layer:

- build on Linux/macOS
- run existing tests
- confirm transport behavior is unchanged

This phase exists to reduce risk before any Windows implementation begins.

### Phase 3: Windows Skeleton Implementation

Add the Windows side behind the same interface:

- Winsock types
- startup/shutdown
- close/error mapping
- polling/waiting equivalents
- address helpers
- socket status flag access / restoration
  - `get_socket_status_flags`
  - `set_socket_status_flags`

At this stage, build success matters more than test completeness.

### Phase 4: Windows Compile Bring-up

Use a Windows environment and fix compile errors iteratively until the target
set builds.

Expected order:

1. core socket portability code
2. TCP/UDP
3. TCP mux
4. any remaining transport-adjacent helpers

### Phase 5: Windows Runtime Validation

After compile succeeds, begin runtime verification in layers:

- core `Endpoint`
- `StorageComm`
- C facade
- Python `cffi`

Make these work on Windows:

- `build_c_endpoint_ffi.py`
- `python/test/test_c_endpoint_smoke.py`
- `python/test/test_c_endpoint_async_smoke.py`
- `python/test/test_endpoint_container_smoke.py`

### Phase 6: Optional Dependencies

Bring up and validate:

1. MQTT
2. Zenoh
3. SHM

This order is recommended because:

- MQTT is relatively standard on Windows
- Zenoh is possible but more sensitive to toolchain/CI behavior
- SHM is likely the most environment-specific

## Recommended Development Sequence

The intended sequence should be:

1. define the portability interface
2. implement and switch POSIX code to it
3. run POSIX regression checks
4. add Windows-side stub implementation
5. enable Windows build selection in CMake
6. compile on Windows and fix errors in order
7. run tests gradually

This is deliberately staged so that:

- refactor risk is separated from platform-port risk
- POSIX regressions are caught before Windows work starts
- partial Windows work can exist without destabilizing current users

## Open Questions

- Should Windows v1 target static linking only, or also DLL consumption?
- Is SHM in scope for first Windows support, or should it be deferred?
- Should `build.bash/install.bash` remain Unix-only while CMake install becomes
  the cross-platform path?
- Should the Python wrapper use `os.add_dll_directory(...)` on Windows?
- Do we want a Windows CI job before optional dependencies are enabled?

## Recommended Direction

Start by proving the narrowest useful slice of the refactor on POSIX first:

- core library
- Storage
- C facade
- Python `cffi`

Then move to Windows.

Do not make Zenoh/MQTT/SHM the first Windows milestone.

The point of the first Windows issue is to identify where the Unix assumptions
are and cut a cross-platform portability layer deliberately, rather than trying
to patch compile errors one by one.
