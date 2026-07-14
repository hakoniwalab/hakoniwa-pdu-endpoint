#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ROOT_DIR="${HAKO_PDU_ENDPOINT_ROOT:-${DEFAULT_ROOT_DIR}}"
OUT_DIR="./rmw-config"
DIRECTION="out"
TYPE_HASH="${RMW_ZENOH_TYPE_HASH:-}"
TYPE_HASH_DIR="${HAKO_RMW_ZENOH_TYPE_HASH_DIR:-}"
RECIPE=""
TOPIC="${HAKO_RMW_ZENOH_TOPIC:-/sample_state}"
ROBOT="StorageDemo"
PDU="sample_state"
ROS_TYPE="std_msgs/msg/UInt64"
DOMAIN_ID="0"
ZENOH_MODE=""
ZENOH_LISTEN_ENDPOINT=""

usage() {
  cat <<'USAGE'
usage: bash tools/make-rmw-zenoh-config.bash [options]

Options:
  --recipe <path>           Recipe YAML/JSON file. Optional for the UInt64 sample.
  --type-hash-dir <path>    Directory containing pdu/type_hash metadata, or the
                            pdu/type_hash directory itself.
  --direction <in|out|inout>
                            Endpoint direction. Default: out, or recipe value.
  --out-dir <path>          Output directory. Default: ./rmw-config
  --type-hash <hash>        Explicit ROS 2 type hash for single-mapping recipes.
  --topic <topic>           ROS 2 topic fallback. Default: /sample_state
  --ros-type <type>         ROS 2 message type fallback. Default: std_msgs/msg/UInt64
  --robot <name>            Hakoniwa robot name fallback. Default: StorageDemo
  --pdu <name>              Hakoniwa PDU name fallback. Default: sample_state
  --domain-id <id>          ROS domain id component for rmw_zenoh keyexpr. Default: 0
  --zenoh-mode <mode>       Zenoh session mode: client, peer, or router.
  --zenoh-listen <endpoint> Generate a listen endpoint instead of connect.
  --root-dir <path>         hakoniwa-pdu-endpoint root for generated endpoint paths.
  -h, --help                Show this help.

Recipe shape:
  direction: out
  domain_id: 0
  pdu_def_path: config/sample/comm/storage_example/pdudef.json
  cache: config/sample/cache/buffer.json
  zenoh:
    endpoint: tcp/127.0.0.1:7447
  mappings:
    - endpoint:
        robot: StorageDemo
        pdu: sample_state
        notify_on_recv: false
      ros2:
        topic: /sample_state
        message_type: std_msgs/msg/UInt64

Environment:
  HAKO_RMW_ZENOH_ROUTER_ENDPOINT  Zenoh router endpoint override.
  HAKO_RMW_ZENOH_ROUTER_HOST      Router host override.
  HAKO_RMW_ZENOH_MODE             Default --zenoh-mode.
  HAKO_RMW_ZENOH_LISTEN_ENDPOINT  Default --zenoh-listen.
  HAKO_RMW_ZENOH_TYPE_HASH_DIR    Default --type-hash-dir.
  RMW_ZENOH_TYPE_HASH             Default --type-hash.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --recipe)
      RECIPE="$2"
      shift 2
      ;;
    --type-hash-dir)
      TYPE_HASH_DIR="$2"
      shift 2
      ;;
    --direction)
      DIRECTION="$2"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --type-hash)
      TYPE_HASH="$2"
      shift 2
      ;;
    --topic)
      TOPIC="$2"
      shift 2
      ;;
    --ros-type|--message-type)
      ROS_TYPE="$2"
      shift 2
      ;;
    --robot)
      ROBOT="$2"
      shift 2
      ;;
    --pdu)
      PDU="$2"
      shift 2
      ;;
    --domain-id)
      DOMAIN_ID="$2"
      shift 2
      ;;
    --zenoh-mode)
      ZENOH_MODE="$2"
      shift 2
      ;;
    --zenoh-listen)
      ZENOH_LISTEN_ENDPOINT="$2"
      shift 2
      ;;
    --root-dir)
      ROOT_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "${DIRECTION}" != "in" && "${DIRECTION}" != "out" && "${DIRECTION}" != "inout" ]]; then
  echo "--direction must be in, out, or inout" >&2
  exit 2
fi

ROS_SETUP="/opt/ros/${ROS_DISTRO:-rolling}/setup.bash"
if [[ -f "${ROS_SETUP}" ]]; then
  set +u
  source "${ROS_SETUP}"
  set -u
fi

ZENOH_ROUTER_HOST="${HAKO_RMW_ZENOH_ROUTER_HOST:-}"
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
fi
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="127.0.0.1"
fi
ZENOH_ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT:-tcp/${ZENOH_ROUTER_HOST}:7447}"
ZENOH_MODE="${ZENOH_MODE:-${HAKO_RMW_ZENOH_MODE:-}}"
ZENOH_LISTEN_ENDPOINT="${ZENOH_LISTEN_ENDPOINT:-${HAKO_RMW_ZENOH_LISTEN_ENDPOINT:-}}"

mkdir -p "${OUT_DIR}"

PY_ARGS=(
  --root-dir "${ROOT_DIR}"
  --out-dir "${OUT_DIR}"
  --direction "${DIRECTION}"
  --type-hash "${TYPE_HASH}"
  --type-hash-dir "${TYPE_HASH_DIR}"
  --topic "${TOPIC}"
  --ros-type "${ROS_TYPE}"
  --robot "${ROBOT}"
  --pdu "${PDU}"
  --domain-id "${DOMAIN_ID}"
  --zenoh-endpoint "${ZENOH_ROUTER_ENDPOINT}"
  --zenoh-mode "${ZENOH_MODE}"
  --zenoh-listen-endpoint "${ZENOH_LISTEN_ENDPOINT}"
)
if [[ -n "${RECIPE}" ]]; then
  PY_ARGS+=(--recipe "${RECIPE}")
fi

python3 "${SCRIPT_DIR}/make_rmw_zenoh_config.py" "${PY_ARGS[@]}"
