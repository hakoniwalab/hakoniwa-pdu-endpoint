#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PYTHON_CMD=${PYTHON_CMD:-python3}
PREFIX=${PREFIX:-/usr/local/hakoniwa}
NATIVE_BUILD_DIR=${NATIVE_BUILD_DIR:-"${PROJECT_ROOT}/build-shared"}
PYTHON_BUILD_DIR=${PYTHON_BUILD_DIR:-"${PROJECT_ROOT}/build/python"}
INSTALL_ROOT=${INSTALL_ROOT:-"${PREFIX}/share/hakoniwa-pdu-endpoint/python"}
PACKAGE_NAME=hakoniwa_pdu_endpoint
PACKAGE_INSTALL_DIR="${INSTALL_ROOT}/${PACKAGE_NAME}"
BUILD_FIRST=${BUILD_FIRST:-ON}

say() {
  printf "%s\n" "$*"
}

die() {
  printf "Error: %s\n" "$*" 1>&2
  exit 1
}

native_lib_dir() {
  case "$(uname -s)" in
    Linux|Darwin)
      printf "%s\n" "${NATIVE_BUILD_DIR}/src"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      printf "%s\n" "${NATIVE_BUILD_DIR}/src/Release"
      ;;
    *)
      if [[ "${OSTYPE:-}" == msys* || "${OSTYPE:-}" == cygwin* || "${OSTYPE:-}" == win32* ]]; then
        printf "%s\n" "${NATIVE_BUILD_DIR}/src/Release"
      else
        printf "%s\n" "${NATIVE_BUILD_DIR}/src"
      fi
      ;;
  esac
}

native_shared_lib() {
  local lib_dir
  lib_dir=$(native_lib_dir)
  case "$(uname -s)" in
    Darwin)
      printf "%s\n" "${lib_dir}/libhakoniwa_pdu_endpoint.dylib"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      printf "%s\n" "${lib_dir}/hakoniwa_pdu_endpoint.dll"
      ;;
    *)
      if [[ "${OSTYPE:-}" == msys* || "${OSTYPE:-}" == cygwin* || "${OSTYPE:-}" == win32* ]]; then
        printf "%s\n" "${lib_dir}/hakoniwa_pdu_endpoint.dll"
      else
        printf "%s\n" "${lib_dir}/libhakoniwa_pdu_endpoint.so"
      fi
      ;;
  esac
}

find_ffi_artifact() {
  local package_build_dir="${PYTHON_BUILD_DIR}/${PACKAGE_NAME}"
  local candidate
  for candidate in "${package_build_dir}"/_c_endpoint_ffi*.so "${package_build_dir}"/_c_endpoint_ffi*.pyd; do
    if [[ -f "${candidate}" ]]; then
      printf "%s\n" "${candidate}"
      return 0
    fi
  done
  return 1
}

ensure_shared_build_dir() {
  if [[ ! -f "${NATIVE_BUILD_DIR}/CMakeCache.txt" ]]; then
    say "Configuring shared native build..."
    cmake -S "${PROJECT_ROOT}" -B "${NATIVE_BUILD_DIR}" -DBUILD_SHARED_LIBS=ON
    return 0
  fi

  if ! grep -q '^BUILD_SHARED_LIBS:BOOL=ON$' "${NATIVE_BUILD_DIR}/CMakeCache.txt"; then
    die "NATIVE_BUILD_DIR=${NATIVE_BUILD_DIR} is not configured with BUILD_SHARED_LIBS=ON"
  fi
}

build_if_needed() {
  if [[ "${BUILD_FIRST}" != "ON" ]]; then
    return 0
  fi

  ensure_shared_build_dir

  say "Building shared native library..."
  cmake --build "${NATIVE_BUILD_DIR}" --target hakoniwa_pdu_endpoint

  local shared_lib
  shared_lib=$(native_shared_lib)
  [[ -f "${shared_lib}" ]] || die "Shared library not found after build: ${shared_lib}"

  say "Building Python cffi module..."
  HAKO_PDU_ENDPOINT_LIB_DIR="$(native_lib_dir)" \
  HAKO_PDU_ENDPOINT_SHARED_LIB="${shared_lib}" \
  PYTHONPATH= \
  "${PYTHON_CMD}" "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"
}

install_python_runtime() {
  local shared_lib
  local ffi_artifact
  shared_lib=$(native_shared_lib)
  ffi_artifact=$(find_ffi_artifact) || die "cffi artifact not found under ${PYTHON_BUILD_DIR}/${PACKAGE_NAME}"

  [[ -f "${shared_lib}" ]] || die "Shared library not found: ${shared_lib}"

  say "Installing Python runtime files to ${PACKAGE_INSTALL_DIR}"
  install -d "${PACKAGE_INSTALL_DIR}"
  cp -R "${PROJECT_ROOT}/python/${PACKAGE_NAME}/." "${PACKAGE_INSTALL_DIR}/"
  install -m 755 "${ffi_artifact}" "${PACKAGE_INSTALL_DIR}/$(basename "${ffi_artifact}")"
  install -m 755 "${shared_lib}" "${PACKAGE_INSTALL_DIR}/$(basename "${shared_lib}")"

  if [[ -d "${PROJECT_ROOT}/config/schema" ]]; then
    install -d "${PACKAGE_INSTALL_DIR}/schema"
    cp -R "${PROJECT_ROOT}/config/schema/." "${PACKAGE_INSTALL_DIR}/schema/"
  fi
}

build_if_needed
install_python_runtime

say "Done."
say "Add this to PYTHONPATH:"
say "  export PYTHONPATH=\"${INSTALL_ROOT}:\$PYTHONPATH\""
say "Optional explicit runtime hints:"
say "  export HAKO_PDU_ENDPOINT_LIB_DIR=\"${PACKAGE_INSTALL_DIR}\""
say "  export HAKO_PDU_ENDPOINT_SHARED_LIB=\"${PACKAGE_INSTALL_DIR}/$(basename "$(native_shared_lib)")\""
