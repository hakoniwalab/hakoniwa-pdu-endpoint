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
  HAKO_PDU_ENDPOINT_ARCHIVE_BASENAME=...
  RUN_SMOKE_TEST=1

Examples:
  bash install-python.bash
  MODE=use-existing PREFIX_DIR=$HOME/.local/lib/hakoniwa-pdu-endpoint bash install-python.bash

Linux ARM64 / aarch64:
  Prebuilt runtime bundles are not published yet. Use the manifest-driven
  source build (`python tools/hako.py doctor`, then `python tools/hako.py build`)
  and `install-python-runtime.bash` instead of bootstrap mode.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

case "$(uname -s)" in
  Linux)
    if [[ -z "${ARCHIVE_BASENAME}" && -z "${RUNTIME_URL}" ]]; then
      case "$(uname -m)" in
        x86_64|amd64)
          ARCHIVE_BASENAME="hakoniwa-pdu-endpoint-linux-x86_64-cp312.zip"
          ;;
        aarch64|arm64)
          if [[ "${MODE}" == "bootstrap" ]]; then
            die "No prebuilt Linux ARM64 runtime bundle is published. Use the manifest-driven source build: 'python tools/hako.py doctor' then 'python tools/hako.py build', followed by 'bash install-python-runtime.bash'."
          fi
          ;;
        *)
          if [[ "${MODE}" == "bootstrap" ]]; then
            die "Unsupported Linux architecture for prebuilt runtime bundle: $(uname -m). Set HAKO_PDU_ENDPOINT_RUNTIME_URL/HAKO_PDU_ENDPOINT_ARCHIVE_BASENAME or use the manifest-driven source build."
          fi
          ;;
      esac
    fi
    ;;
  Darwin)
    if [[ -z "${ARCHIVE_BASENAME}" && -z "${RUNTIME_URL}" ]]; then
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

if [[ -z "${ARCHIVE_BASENAME}" && -n "${RUNTIME_URL}" ]]; then
  ARCHIVE_BASENAME="${RUNTIME_URL##*/}"
fi

if [[ -z "${RUNTIME_URL}" && -n "${ARCHIVE_BASENAME}" ]]; then
  RUNTIME_URL="https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/releases/download/${VERSION}/${ARCHIVE_BASENAME}"
fi

install_python_packages() {
  say "Installing Python packages..."
  "${PIP_CMD[@]}" install --upgrade pip setuptools wheel cffi
  "${PIP_CMD[@]}" install --upgrade hakoniwa-pdu hakoniwa-pdu-endpoint
}

download_runtime_bundle() {
  [[ -n "${ARCHIVE_BASENAME}" ]] || die "Runtime archive basename is empty"
  [[ -n "${RUNTIME_URL}" ]] || die "Runtime URL is empty"
  local archive_path="${PREFIX_DIR}/${ARCHIVE_BASENAME}"
  mkdir -p "${PREFIX_DIR}"
  say "Downloading runtime bundle..."
  curl -L --fail --output "${archive_path}" "${RUNTIME_URL}"
  say "Extracting runtime bundle..."
  unzip -o "${archive_path}" -d "${PREFIX_DIR}" >/dev/null
  rm -f "${archive_path}"
}

say "MODE=${MODE}"
say "PYTHON_CMD=${PYTHON_CMD}"
say "PREFIX_DIR=${PREFIX_DIR}"
say "RUNTIME_URL=${RUNTIME_URL:-<none>}"

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

say "Downloaded runtime bundle into: ${PREFIX_DIR}"
say "Next step: overlay runtime files into the installed Python package:"
say "  bash install-python-runtime.bash"
say "If the target Python package directory is system-owned, run only that overlay step with sudo."
say "Done."
