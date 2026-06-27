#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${HAKO_PDU_ENDPOINT_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
exec bash "${ROOT_DIR}/tools/make-rmw-zenoh-config.bash" "$@"
