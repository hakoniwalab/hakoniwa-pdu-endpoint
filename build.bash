#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}

say() {
  printf "%s
" "$*"
}

detect_parallel_level() {
  if command -v nproc >/dev/null 2>&1; then
    nproc 2>/dev/null && return
  fi
  if command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.ncpu 2>/dev/null && return
  fi
  printf "1\n"
}

CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-$(detect_parallel_level)}

say "--- Configuring C++ Project (${BUILD_TYPE}) ---"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBUILD_SHARED_LIBS=OFF

say "--- Building C++ Core Library ---"
cmake --build "${BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL}"

say "Build complete."
