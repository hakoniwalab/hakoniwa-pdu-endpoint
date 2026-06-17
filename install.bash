#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
PREFIX=${PREFIX:-/usr/local/hakoniwa}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_ROOT}/build"
PY_SRC_DIR="${PROJECT_ROOT}/python"
PY_INSTALL_DIR="${PREFIX}/share/hakoniwa-pdu-endpoint/python"
PY_PKG_INSTALL_DIR="${PY_INSTALL_DIR}/hakoniwa_pdu_endpoint"

say() {
  printf "%s
" "$*"
}

die() {
  printf "Error: %s
" "$*" 1>&2
  exit 1
}

# --- 1. Build Step ---
say "--- Ensuring project is built ---"
if [[ ! -d "${BUILD_DIR}" ]]; then
  say "Build directory not found. Running build.bash..."
  bash "${PROJECT_ROOT}/build.bash"
else
  say "Build directory found. Assuming project is already built."
fi

# --- 2. C++ Component Installation ---
say "--- Installing C++ components to ${PREFIX} ---"
if [[ ! -w "${PREFIX}" ]]; then
  say "Note: Prefix '${PREFIX}' is not writable. This step may require sudo."
fi
cd "${BUILD_DIR}"
cmake --install . --prefix "${PREFIX}"
cd "${PROJECT_ROOT}"


# --- 3. Python Package Installation ---
say "--- Installing Python package to ${PY_INSTALL_DIR} ---"

# 3.1. Create Python package directory
install -d "${PY_PKG_INSTALL_DIR}"

# 3.2. Copy Python source files (.py)
say "Copying Python source files..."
cp -R "${PY_SRC_DIR}/hakoniwa_pdu_endpoint/." "${PY_PKG_INSTALL_DIR}/"

# 3.3. Copy Python FFI extension (.so)
say "Copying Python FFI extension..."
SO_FILE=$(find "${BUILD_DIR}" -name "_c_endpoint_ffi*.so" | head -n 1)
if [[ -z "$SO_FILE" || ! -f "$SO_FILE" ]]; then
  die "Python FFI module (_c_endpoint_ffi*.so) not found in build directory. The Python build may have failed or was not run."
fi
cp "$SO_FILE" "${PY_PKG_INSTALL_DIR}/"
say "  Copied $SO_FILE"

# 3.4. Copy core dependency library (.dylib) into the package
say "Copying core C++ library for Python package..."
CORE_DYLIB=$(find "${BUILD_DIR}" -name "libhakoniwa_pdu_endpoint.dylib" | head -n 1)
if [[ -z "$CORE_DYLIB" || ! -f "$CORE_DYLIB" ]]; then
  die "Core library (libhakoniwa_pdu_endpoint.dylib) not found in build directory."
fi
cp "$CORE_DYLIB" "${PY_PKG_INSTALL_DIR}/"
say "  Copied $CORE_DYLIB"

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
