#!/usr/bin/env bash
set -euo pipefail

PREFIX=${PREFIX:-/usr/local/hakoniwa}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LIB_NAME=libhakoniwa_pdu_endpoint.a
PY_INSTALL_DIR="${PREFIX}/share/hakoniwa-pdu-endpoint/python"

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

say "Removing ${LIB_NAME} from ${PREFIX}/lib (if present)"
if [[ -f "${PREFIX}/lib/${LIB_NAME}" ]]; then
  rm -f "${PREFIX}/lib/${LIB_NAME}"
fi

say "Removing installed headers from ${PREFIX}/include"
while IFS= read -r src; do
  rel=${src#"${PROJECT_ROOT}/include/"}
  dest="${PREFIX}/include/${rel}"
  if [[ -f "${dest}" ]]; then
    rm -f "${dest}"
  fi
done < <(find "${PROJECT_ROOT}/include" -type f \( -name "*.h" -o -name "*.hpp" \) | sort)

if [[ -d "${PREFIX}/include/hakoniwa" ]]; then
  find "${PREFIX}/include/hakoniwa" -type d -empty -delete
fi

if [[ -d "${PY_INSTALL_DIR}" ]]; then
  say "Removing Python package from ${PY_INSTALL_DIR}"
  rm -rf "${PY_INSTALL_DIR}"
fi

say "Done."
