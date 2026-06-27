#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/workspace/hakoniwa-pdu-endpoint"
TMP_DIR="/tmp/hako-rmw-zenoh-test"
ENDPOINT_CFG="${TMP_DIR}/endpoint_rmw_zenoh_sub.json"
COMM_CFG="${TMP_DIR}/rmw_zenoh_sub_comm.json"
ENDPOINT_LOG="${TMP_DIR}/endpoint_sub.log"
ROUTER_LOG="${TMP_DIR}/rmw_zenohd.log"

source "/opt/ros/${ROS_DISTRO}/setup.bash"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_zenoh_cpp}"

mkdir -p "${TMP_DIR}"
cd "${ROOT_DIR}"

TYPE_HASH="${RMW_ZENOH_TYPE_HASH:-}"
if [[ -z "${TYPE_HASH}" ]]; then
  TYPE_HASH="$(ros2 interface type_hash std_msgs/msg/UInt64 2>/dev/null | grep -Eo 'RIHS[0-9A-Za-z_]+' | head -n1 || true)"
fi

if [[ -z "${TYPE_HASH}" ]]; then
  echo "Failed to resolve std_msgs/msg/UInt64 type hash." >&2
  echo "Set RMW_ZENOH_TYPE_HASH explicitly and rerun this script." >&2
  exit 2
fi

python3 - "${TYPE_HASH}" "${COMM_CFG}" "${ENDPOINT_CFG}" <<'PY'
import json
import sys

type_hash, comm_path, endpoint_path = sys.argv[1:4]
root = "/workspace/hakoniwa-pdu-endpoint"

comm = {
    "protocol": "rmw_zenoh",
    "name": "rmw_zenoh_sub_docker",
    "direction": "in",
    "rmw_zenoh": {
        "config_path": f"{root}/config/sample/comm/zenoh/client.json5",
        "domain_id": 0,
        "timestamp": {"source": "system_clock"},
        "mappings": [
            {
                "robot": "StorageDemo",
                "pdu": "sample_state",
                "topic": "/sample_state",
                "type": "auto",
                "type_hash": type_hash,
                "gid": "auto",
                "notify_on_recv": True,
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
    "name": "sample_rmw_zenoh_sub_endpoint_docker",
    "pdu_def_path": f"{root}/config/sample/comm/storage_example/pdudef.json",
    "cache": f"{root}/config/sample/cache/buffer.json",
    "comm": comm_path,
}

with open(comm_path, "w", encoding="utf-8") as f:
    json.dump(comm, f, indent=2)
with open(endpoint_path, "w", encoding="utf-8") as f:
    json.dump(endpoint, f, indent=2)
PY

echo "Using std_msgs/msg/UInt64 type_hash=${TYPE_HASH}"

ros2 run rmw_zenoh_cpp rmw_zenohd >"${ROUTER_LOG}" 2>&1 &
ROUTER_PID=$!
./build-docker/examples/endpoint_zenoh_sub "${ENDPOINT_CFG}" >"${ENDPOINT_LOG}" 2>&1 &
ENDPOINT_PID=$!

cleanup() {
  kill "${ENDPOINT_PID}" "${ROUTER_PID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 2
python3 docker/ros/rmw_zenoh_uint64_pub.py
wait "${ENDPOINT_PID}" || true

echo "----- endpoint log -----"
cat "${ENDPOINT_LOG}"
echo "----- rmw_zenohd log -----"
cat "${ROUTER_LOG}"

if ! grep -Eq 'received (sample_state=|[0-9]+ bytes)' "${ENDPOINT_LOG}"; then
  echo "Endpoint did not receive a ROS 2 rmw_zenoh sample." >&2
  exit 1
fi

