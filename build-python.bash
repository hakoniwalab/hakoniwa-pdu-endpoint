#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
PYTHON_CMD=${PYTHON_CMD:-python3}

case "$(uname -s)" in
  Darwin)
    SHARED_LIB="${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.dylib"
    ;;
  Linux)
    SHARED_LIB="${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.so"
    ;;
  *)
    echo "Error: unsupported OS: $(uname -s)" >&2
    exit 1
    ;;
esac

if [[ ! -f "${SHARED_LIB}" ]]; then
  echo "Error: native shared library not found: ${SHARED_LIB}" >&2
  echo "Native build must be completed first." >&2
  echo "Run: BUILD_SHARED=ON bash build.bash" >&2
  exit 1
fi

say() {
  printf "%s\n" "$*"
}


say "Building Python cffi module..."

env_args=(
  "HAKO_PDU_ENDPOINT_LIB_DIR=${BUILD_DIR}/src"
  "HAKO_PDU_ENDPOINT_SHARED_LIB=${SHARED_LIB}"
  "HAKO_PDU_ENDPOINT_PYTHON_BUILD_DIR=${BUILD_DIR}/python"
)

env "${env_args[@]}" PYTHONPATH= \
  "${PYTHON_CMD}" \
  "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"

say "Done."
