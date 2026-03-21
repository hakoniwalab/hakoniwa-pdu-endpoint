#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_SHARED=${BUILD_SHARED:-OFF}

say() {
  printf "%s\n" "$*"
}

say "Configuring (${BUILD_TYPE})..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS="${BUILD_SHARED}"

say "Building..."
cmake --build "${BUILD_DIR}"

say "Done."
