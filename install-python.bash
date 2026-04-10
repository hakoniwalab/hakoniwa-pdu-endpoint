#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${MODE:-bootstrap}"
PYTHON_CMD="${PYTHON_CMD:-python3}"
PIP_CMD=("${PYTHON_CMD}" -m pip)
PREFIX_DIR="${PREFIX_DIR:-$HOME/.local/lib/hakoniwa-pdu-endpoint}"
VERSION="${HAKO_PDU_ENDPOINT_VERSION:-v1.0.0}"
ARCHIVE_BASENAME="${HAKO_PDU_ENDPOINT_ARCHIVE_BASENAME:-}"
RUNTIME_URL="${HAKO_PDU_ENDPOINT_RUNTIME_URL:-}"
RUN_SMOKE_TEST="${RUN_SMOKE_TEST:-1}"

say() {
  printf "%s\n" "$*"
}

die() {
  printf "Error: %s\n" "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  bash install-python.bash

Env:
  MODE=bootstrap|use-existing
  PYTHON_CMD=python3
  PREFIX_DIR=$HOME/.local/lib/hakoniwa-pdu-endpoint
  HAKO_PDU_ENDPOINT_VERSION=v1.0.0
  HAKO_PDU_ENDPOINT_RUNTIME_URL=https://...
  RUN_SMOKE_TEST=1

Examples:
  bash install-python.bash
  MODE=use-existing PREFIX_DIR=$HOME/.local/lib/hakoniwa-pdu-endpoint bash install-python.bash
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

case "$(uname -s)" in
  Linux)
    if [[ -z "${ARCHIVE_BASENAME}" ]]; then
      ARCHIVE_BASENAME="hakoniwa-pdu-endpoint-linux-x86_64-cp312.zip"
    fi
    ;;
  Darwin)
    if [[ -z "${ARCHIVE_BASENAME}" ]]; then
      case "$(uname -m)" in
        arm64) ARCHIVE_BASENAME="hakoniwa-pdu-endpoint-macos-arm64-cp312.zip" ;;
        x86_64) ARCHIVE_BASENAME="hakoniwa-pdu-endpoint-macos-x86_64-cp312.zip" ;;
        *) die "Unsupported macOS architecture: $(uname -m)" ;;
      esac
    fi
    ;;
  *)
    die "Unsupported OS: $(uname -s)"
    ;;
esac

if [[ -z "${RUNTIME_URL}" ]]; then
  RUNTIME_URL="https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/releases/download/${VERSION}/${ARCHIVE_BASENAME}"
fi

runtime_shared_lib_path() {
  case "$(uname -s)" in
    Linux) printf "%s/%s\n" "${PREFIX_DIR}" "hakoniwa_pdu_endpoint-linux-x86_64.so" ;;
    Darwin)
      case "$(uname -m)" in
        arm64) printf "%s/%s\n" "${PREFIX_DIR}" "hakoniwa_pdu_endpoint-macos-arm64.dylib" ;;
        x86_64) printf "%s/%s\n" "${PREFIX_DIR}" "hakoniwa_pdu_endpoint-macos-x86_64.dylib" ;;
      esac
      ;;
  esac
}

install_python_packages() {
  say "Installing Python packages..."
  "${PIP_CMD[@]}" install --upgrade pip setuptools wheel cffi
  "${PIP_CMD[@]}" install --upgrade hakoniwa-pdu hakoniwa-pdu-endpoint
}

download_runtime_bundle() {
  local archive_path="${PREFIX_DIR}/${ARCHIVE_BASENAME}"
  mkdir -p "${PREFIX_DIR}"
  say "Downloading runtime bundle..."
  curl -L --fail --output "${archive_path}" "${RUNTIME_URL}"
  say "Extracting runtime bundle..."
  unzip -o "${archive_path}" -d "${PREFIX_DIR}" >/dev/null
  rm -f "${archive_path}"
}

run_smoke_test() {
  local lib_path
  lib_path="$(runtime_shared_lib_path)"
  say "Running smoke test..."
  env \
    HAKO_PDU_ENDPOINT_SHARED_LIB="${lib_path}" \
    HAKO_PDU_ENDPOINT_LIB_DIR="${PREFIX_DIR}" \
    PYTHONPATH= \
    "${PYTHON_CMD}" -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
}

say "MODE=${MODE}"
say "PYTHON_CMD=${PYTHON_CMD}"
say "PREFIX_DIR=${PREFIX_DIR}"
say "RUNTIME_URL=${RUNTIME_URL}"

case "${MODE}" in
  bootstrap)
    install_python_packages
    download_runtime_bundle
    ;;
  use-existing)
    say "Using existing Python/runtime installation"
    ;;
  *)
    die "Unsupported MODE='${MODE}' (expected bootstrap|use-existing)"
    ;;
esac

say "Set these environment variables before using the Python binding:"
say "  export HAKO_PDU_ENDPOINT_SHARED_LIB=$(runtime_shared_lib_path)"
say "  export HAKO_PDU_ENDPOINT_LIB_DIR=${PREFIX_DIR}"

if [[ "${RUN_SMOKE_TEST}" == "1" ]]; then
  run_smoke_test
fi

say "Done."
