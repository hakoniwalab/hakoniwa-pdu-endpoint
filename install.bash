#!/usr/bin/env bash
set -euo pipefail

PREFIX=${PREFIX:-/usr/local/hakoniwa}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
LIB_NAME=libhakoniwa_pdu_endpoint.a

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

LIB_SRC=""
for candidate in "${BUILD_DIR}/src/${LIB_NAME}" "${BUILD_DIR}/${LIB_NAME}"; do
  if [[ -f "${candidate}" ]]; then
    LIB_SRC="${candidate}"
    break
  fi
done

if [[ -z "${LIB_SRC}" ]]; then
  die "${LIB_NAME} not found. Build first (expected in ${BUILD_DIR}/src or ${BUILD_DIR})."
fi

say "Installing headers to ${PREFIX}/include"
install -d "${PREFIX}/include"

while IFS= read -r src; do
  rel=${src#"${PROJECT_ROOT}/include/"}
  dest="${PREFIX}/include/${rel}"
  install -d "$(dirname "${dest}")"
  install -m 644 "${src}" "${dest}"
done < <(find "${PROJECT_ROOT}/include" -type f \( -name "*.h" -o -name "*.hpp" \) | sort)

say "Installing ${LIB_NAME} to ${PREFIX}/lib"
install -d "${PREFIX}/lib"
install -m 644 "${LIB_SRC}" "${PREFIX}/lib/${LIB_NAME}"

say "Done."
