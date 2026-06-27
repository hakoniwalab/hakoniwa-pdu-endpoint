#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/workspace/hakoniwa-pdu-endpoint"
OUT_DIR="/tmp/hako-rmw-zenoh-test"
DIRECTION="out"
TYPE_HASH="${RMW_ZENOH_TYPE_HASH:-}"
TOPIC="${HAKO_RMW_ZENOH_TOPIC:-/sample_state}"
ROBOT="StorageDemo"
PDU="sample_state"
DOMAIN_ID="0"

usage() {
  cat <<'USAGE'
usage: bash docker/make-rmw-zenoh-config.bash [options]

Options:
  --direction <in|out>      Endpoint direction. Default: out
  --out-dir <path>          Output directory. Default: /tmp/hako-rmw-zenoh-test
  --type-hash <hash>        ROS 2 type hash. Default: RMW_ZENOH_TYPE_HASH or ros2-type-hash
  --topic <topic>           ROS 2 topic. Default: /sample_state
  --robot <name>            Hakoniwa robot name. Default: StorageDemo
  --pdu <name>              Hakoniwa PDU name. Default: sample_state
  --domain-id <id>          ROS domain id component for rmw_zenoh keyexpr. Default: 0
  -h, --help                Show this help.

Environment:
  HAKO_RMW_ZENOH_ROUTER_ENDPOINT  Zenoh router endpoint override.
  HAKO_RMW_ZENOH_ROUTER_HOST      Router host override.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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

if [[ "${DIRECTION}" != "in" && "${DIRECTION}" != "out" ]]; then
  echo "--direction must be in or out" >&2
  exit 2
fi

set +u
source "/opt/ros/${ROS_DISTRO:-rolling}/setup.bash"
set -u

if [[ -z "${TYPE_HASH}" ]]; then
  TYPE_HASH="$(ros2-type-hash std_msgs/msg/UInt64 2>/dev/null || true)"
fi
if [[ -z "${TYPE_HASH}" ]]; then
  TYPE_HASH="$(ros2 interface type_hash std_msgs/msg/UInt64 2>/dev/null | grep -Eo 'RIHS[0-9A-Za-z_]+' | head -n1 || true)"
fi
if [[ -z "${TYPE_HASH}" ]]; then
  echo "Failed to resolve std_msgs/msg/UInt64 type hash." >&2
  echo "Set RMW_ZENOH_TYPE_HASH or pass --type-hash." >&2
  exit 2
fi
if [[ "${DIRECTION}" == "out" && "${TYPE_HASH}" == "*" ]]; then
  echo "Publisher config requires a concrete type hash." >&2
  exit 2
fi

ZENOH_ROUTER_HOST="${HAKO_RMW_ZENOH_ROUTER_HOST:-}"
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
fi
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="127.0.0.1"
fi
ZENOH_ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT:-tcp/${ZENOH_ROUTER_HOST}:7447}"

mkdir -p "${OUT_DIR}"

python3 - "${DIRECTION}" "${TYPE_HASH}" "${OUT_DIR}" "${ZENOH_ROUTER_ENDPOINT}" "${TOPIC}" "${ROBOT}" "${PDU}" "${DOMAIN_ID}" <<'PY'
import json
import sys

direction, type_hash, out_dir, zenoh_endpoint, topic, robot, pdu, domain_id = sys.argv[1:9]
root = "/workspace/hakoniwa-pdu-endpoint"
role = "sub" if direction == "in" else "pub"

endpoint_path = f"{out_dir}/endpoint_rmw_zenoh_{role}.json"
comm_path = f"{out_dir}/rmw_zenoh_{role}_comm.json"
zenoh_path = f"{out_dir}/zenoh_client_{role}.json5"

zenoh = {
    "mode": "client",
    "connect": {
        "endpoints": [
            zenoh_endpoint,
        ],
    },
}

comm = {
    "protocol": "rmw_zenoh",
    "name": f"rmw_zenoh_{direction}_manual",
    "direction": direction,
    "rmw_zenoh": {
        "config_path": zenoh_path,
        "domain_id": int(domain_id),
        "timestamp": {"source": "system_clock"},
        "mappings": [
            {
                "robot": robot,
                "pdu": pdu,
                "topic": topic,
                "type": "auto",
                "type_hash": type_hash,
                "gid": "auto",
                "notify_on_recv": direction != "out",
                "qos": {
                    "reliability": "best_effort",
                    "durability": "volatile",
                    "history": "keep_last",
                    "depth": 10,
                },
            }
        ],
    },
}

endpoint = {
    "name": f"sample_rmw_zenoh_{direction}_endpoint_manual",
    "pdu_def_path": f"{root}/config/sample/comm/storage_example/pdudef.json",
    "cache": f"{root}/config/sample/cache/buffer.json",
    "comm": comm_path,
}

for path, data in [
    (zenoh_path, zenoh),
    (comm_path, comm),
    (endpoint_path, endpoint),
]:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)

print(endpoint_path)
PY
