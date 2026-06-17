#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Release"

say() {
  printf "%s
" "$*"
}

say "--- Configuring C++ Project (${BUILD_TYPE}) ---"
# BUILD_SHARED_LIBS is now ON by default in CMakeLists.txt
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

say "--- Building C++ Core Library ---"
# Use parallel build to speed up
cmake --build "${BUILD_DIR}" -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

say "--- Building Python FFI Module ---"
python3 "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"

say "Build complete."
