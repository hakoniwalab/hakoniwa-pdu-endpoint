#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
BUILD_NATIVE=${BUILD_NATIVE:-ON}
BUILD_FFI=${BUILD_FFI:-ON}
BUILD_SHARED_LIBS=${BUILD_SHARED_LIBS:-OFF}
PYTHON_CMD=${PYTHON_CMD:-python3}

say() {
  printf "%s\n" "$*"
}

if [[ "${BUILD_NATIVE}" == "ON" ]]; then
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    say "Configuring native build..."
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DBUILD_SHARED_LIBS="${BUILD_SHARED_LIBS}"
  fi

  say "Building native library..."
  cmake --build "${BUILD_DIR}" --target hakoniwa_pdu_endpoint
fi

if [[ "${BUILD_FFI}" == "ON" ]]; then
  say "Building Python cffi module..."
  env_args=()
  if [[ "${BUILD_SHARED_LIBS}" == "ON" ]]; then
    case "$(uname -s)" in
      Darwin)
        env_args+=("HAKO_PDU_ENDPOINT_LIB_DIR=${BUILD_DIR}/src")
        env_args+=("HAKO_PDU_ENDPOINT_SHARED_LIB=${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.dylib")
        ;;
      *)
        env_args+=("HAKO_PDU_ENDPOINT_LIB_DIR=${BUILD_DIR}/src")
        env_args+=("HAKO_PDU_ENDPOINT_SHARED_LIB=${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.so")
        ;;
    esac
  fi
  env "${env_args[@]}" PYTHONPATH= "${PYTHON_CMD}" "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"
fi

say "Done."
