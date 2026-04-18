#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build}"
PREFIX_DIR="${PREFIX_DIR:-/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint}"
PYTHON_CMD="${PYTHON_CMD:-python3}"
RUN_SMOKE_TEST="${RUN_SMOKE_TEST:-1}"

say() {
  printf "%s\n" "$*"
}

die() {
  printf "Error: %s\n" "$*" >&2
  exit 1
}

case "$(uname -s)" in
  Darwin)
    SHARED_LIB="${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.dylib"
    ;;
  Linux)
    SHARED_LIB="${BUILD_DIR}/src/libhakoniwa_pdu_endpoint.so"
    ;;
  *)
    die "Unsupported OS: $(uname -s)"
    ;;
esac

FFI_EXT="$(find "${BUILD_DIR}/python/hakoniwa_pdu_endpoint" -type f -name '_c_endpoint_ffi*.so' | head -n 1)"
SRC_PY_DIR="${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint"
DST_PY_DIR="${PREFIX_DIR}/python/hakoniwa_pdu_endpoint"
SCHEMA_SRC_DIR="${PROJECT_ROOT}/config/schema"

[[ -f "${SHARED_LIB}" ]] || die "Shared library not found: ${SHARED_LIB}. Run: BUILD_SHARED=ON bash build.bash"
[[ -n "${FFI_EXT}" ]] || die "FFI extension not found under ${BUILD_DIR}/python/hakoniwa_pdu_endpoint. Run: bash build-python.bash"
[[ -d "${SRC_PY_DIR}" ]] || die "Python source package not found: ${SRC_PY_DIR}"

say "Installing Python runtime to: ${DST_PY_DIR}"
install -d "${DST_PY_DIR}"

say "Copying pure Python files..."
cp -R "${SRC_PY_DIR}/." "${DST_PY_DIR}/"

if [[ -d "${SCHEMA_SRC_DIR}" ]]; then
  say "Copying schema files..."
  install -d "${DST_PY_DIR}/schema"
  cp -R "${SCHEMA_SRC_DIR}/." "${DST_PY_DIR}/schema/"
fi

say "Copying native runtime..."
cp -f "${SHARED_LIB}" "${DST_PY_DIR}/$(basename "${SHARED_LIB}")"
cp -f "${FFI_EXT}" "${DST_PY_DIR}/$(basename "${FFI_EXT}")"

say "Installed:"
say "  ${DST_PY_DIR}/$(basename "${SHARED_LIB}")"
say "  ${DST_PY_DIR}/$(basename "${FFI_EXT}")"

if [[ "$(uname -s)" == "Darwin" ]]; then
  install_name_tool -change \
    @rpath/libhakoniwa_pdu_endpoint.dylib \
    @loader_path/libhakoniwa_pdu_endpoint.dylib \
    "${DST_PY_DIR}/$(basename "${FFI_EXT}")"
fi


if [[ "${RUN_SMOKE_TEST}" == "1" ]]; then
  say "Running smoke test..."
  env \
    PYTHONPATH="${PREFIX_DIR}/python" \
    HAKO_PDU_ENDPOINT_LIB_DIR="${DST_PY_DIR}" \
    HAKO_PDU_ENDPOINT_SHARED_LIB="${DST_PY_DIR}/$(basename "${SHARED_LIB}")" \
    "${PYTHON_CMD}" -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
fi

say "Done."