#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
PREFIX=${PREFIX:-/usr/local/hakoniwa}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
PY_BUILD_DIR=${PY_BUILD_DIR:-"${PROJECT_ROOT}/build-py"}
BUILD_TYPE=${BUILD_TYPE:-Release}
PYTHON_BIN=${PYTHON_BIN:-python3}
PY_SRC_DIR="${PROJECT_ROOT}/python"
PY_INSTALL_DIR="${PREFIX}/share/hakoniwa-pdu-endpoint/python"
PY_PKG_INSTALL_DIR="${PY_INSTALL_DIR}/hakoniwa_pdu_endpoint"

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

ensure_python_cffi() {
  if "${PYTHON_BIN}" -c "import cffi" >/dev/null 2>&1; then
    return
  fi

  if [[ "$(uname -s)" == "Linux" && "${EUID:-$(id -u)}" -eq 0 ]] && command -v apt-get >/dev/null 2>&1; then
    say "Python cffi module not found for ${PYTHON_BIN}; installing python3-cffi with apt-get..."
    apt-get install -y python3-cffi
    "${PYTHON_BIN}" -c "import cffi" >/dev/null 2>&1 && return
  fi

  die "Python cffi module is required for ${PYTHON_BIN}. Install it first, e.g. 'sudo apt install -y python3-cffi' on Ubuntu."
}

die() {
  printf "Error: %s
" "$*" 1>&2
  exit 1
}

case "$(uname -s)" in
  Darwin)
    CORE_LIB_PATTERN="libhakoniwa_pdu_endpoint.dylib"
    ;;
  Linux)
    CORE_LIB_PATTERN="libhakoniwa_pdu_endpoint.so"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    CORE_LIB_PATTERN="hakoniwa_pdu_endpoint.dll"
    ;;
  *)
    die "Unsupported OS: $(uname -s)"
    ;;
esac
CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-$(detect_parallel_level)}
ensure_python_cffi

# --- 1. Build Step ---
say "--- Building static C++ components (${BUILD_TYPE}) ---"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build "${BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL}"

say "--- Building shared C++ library for Python/CFFI (${BUILD_TYPE}) ---"
cmake -S "${PROJECT_ROOT}" -B "${PY_BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBUILD_SHARED_LIBS=ON
cmake --build "${PY_BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL}" --target hakoniwa_pdu_endpoint

CORE_LIB=$(find "${PY_BUILD_DIR}" -name "${CORE_LIB_PATTERN}" | head -n 1)
if [[ -z "$CORE_LIB" || ! -f "$CORE_LIB" ]]; then
  die "Core library (${CORE_LIB_PATTERN}) not found in shared build directory."
fi

say "--- Building Python FFI module ---"
HAKO_PDU_ENDPOINT_SHARED_LIB="${CORE_LIB}" \
  HAKO_PDU_ENDPOINT_PYTHON_BUILD_DIR="${PY_BUILD_DIR}/python" \
  "${PYTHON_BIN}" "${PROJECT_ROOT}/python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py"

# --- 2. C++ Component Installation ---
say "--- Installing C++ components to ${PREFIX} ---"
if [[ ! -w "${PREFIX}" ]]; then
  say "Note: Prefix '${PREFIX}' is not writable. This step may require sudo."
fi
cd "${BUILD_DIR}"
cmake --install . --prefix "${PREFIX}"
cd "${PROJECT_ROOT}"

say "Installing shared C++ library for Python/CFFI to ${PREFIX}/lib"
install -d "${PREFIX}/lib"
cp "${CORE_LIB}" "${PREFIX}/lib/"

# --- 3. Python Package Installation ---
say "--- Installing Python package to ${PY_INSTALL_DIR} ---"

# 3.1. Create Python package directory
install -d "${PY_PKG_INSTALL_DIR}"

# 3.2. Copy Python source files (.py)
say "Copying Python source files..."
cp -R "${PY_SRC_DIR}/hakoniwa_pdu_endpoint/." "${PY_PKG_INSTALL_DIR}/"

# 3.3. Copy Python FFI extension (.so)
say "Copying Python FFI extension..."
SO_FILE=$(find "${PY_BUILD_DIR}/python" -name "_c_endpoint_ffi*.so" | head -n 1)
if [[ -z "$SO_FILE" || ! -f "$SO_FILE" ]]; then
  die "Python FFI module (_c_endpoint_ffi*.so) not found in build directory. The Python build may have failed or was not run."
fi
cp "$SO_FILE" "${PY_PKG_INSTALL_DIR}/"
say "  Copied $SO_FILE"

# 3.4. Copy core dependency library into the package
say "Copying core C++ library for Python package..."
cp "$CORE_LIB" "${PY_PKG_INSTALL_DIR}/"
say "  Copied $CORE_LIB"

# 3.5. Copy schema files
if [[ -d "${PROJECT_ROOT}/config/schema" ]]; then
  say "Copying schema files..."
  install -d "${PY_PKG_INSTALL_DIR}/schema"
  cp -R "${PROJECT_ROOT}/config/schema/." "${PY_PKG_INSTALL_DIR}/schema/"
fi

# --- Final Instructions ---
say ""
say "Installation complete."
say "The Python package has been installed to: ${PY_INSTALL_DIR}"
say "To use it, add it to your PYTHONPATH:"
say "  export PYTHONPATH="${PY_INSTALL_DIR}:\$PYTHONPATH""
