#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
BUILD_NATIVE=${BUILD_NATIVE:-ON}
BUILD_FFI=${BUILD_FFI:-ON}

say() {
  printf "%s\n" "$*"
}

if [[ "${BUILD_NATIVE}" == "ON" ]]; then
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    say "Configuring native build..."
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
  fi

  say "Building native library..."
  cmake --build "${BUILD_DIR}" --target hakoniwa_pdu_endpoint
fi

if [[ "${BUILD_FFI}" == "ON" ]]; then
  say "Building Python cffi module..."
  python3 "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"
fi

say "Done."
