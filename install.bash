#!/usr/bin/env bash
set -euo pipefail

PREFIX=${PREFIX:-/usr/local/hakoniwa}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
LIB_NAME=libhakoniwa_pdu_endpoint.a
PY_PACKAGE_DIR=python
PY_INSTALL_DIR="${PREFIX}/share/hakoniwa-pdu-endpoint/python"
CM_INSTALL_DIR="${PREFIX}/lib/cmake/hakoniwa_pdu_endpoint"

say() {
  printf "%s\n" "$*"
}

die() {
  printf "Error: %s\n" "$*" 1>&2
  exit 1
}

if [[ ! -d "${PROJECT_ROOT}/include" ]]; then
  die "include/ directory not found at ${PROJECT_ROOT}/include"
fi

if [[ ! -w "${PREFIX}" ]]; then
  say "Note: writing to ${PREFIX} usually requires sudo."
fi

if [[ ! -d "${BUILD_DIR}" || ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  die "CMake build directory not found at ${BUILD_DIR}. Run cmake configure/build first."
fi

say "Installing CMake package to ${PREFIX}"
cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"

if [[ ! -d "${CM_INSTALL_DIR}" ]]; then
  die "CMake package files not found at ${CM_INSTALL_DIR}. Check install output."
fi

if [[ -d "${PROJECT_ROOT}/${PY_PACKAGE_DIR}/hakoniwa_pdu_endpoint" ]]; then
  say "Installing Python package to ${PY_INSTALL_DIR}"
  install -d "${PY_INSTALL_DIR}"
  cp -R "${PROJECT_ROOT}/${PY_PACKAGE_DIR}/hakoniwa_pdu_endpoint" "${PY_INSTALL_DIR}/"
  if [[ -d "${PROJECT_ROOT}/config/schema" ]]; then
    install -d "${PY_INSTALL_DIR}/hakoniwa_pdu_endpoint/schema"
    cp -R "${PROJECT_ROOT}/config/schema/." "${PY_INSTALL_DIR}/hakoniwa_pdu_endpoint/schema/"
  fi
fi

say "Done."
say "For python -m usage:"
say "  export PYTHONPATH=\"${PY_INSTALL_DIR}:\$PYTHONPATH\""
