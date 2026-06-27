#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/workspace/hakoniwa-pdu-endpoint"
TMP_DIR="/tmp/hako-rmw-zenoh-test"
ENDPOINT_CFG="${TMP_DIR}/endpoint_rmw_zenoh_pub.json"
ENDPOINT_LOG="${TMP_DIR}/endpoint_pub_cdr.log"
ROS_SUB_LOG="${TMP_DIR}/ros_sub.log"
ROUTER_LOG="${TMP_DIR}/rmw_zenohd_pub.log"

set +u
source "/opt/ros/${ROS_DISTRO}/setup.bash"
set -u
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_zenoh_cpp}"

mkdir -p "${TMP_DIR}"
cd "${ROOT_DIR}"

ZENOH_ROUTER_HOST="${HAKO_RMW_ZENOH_ROUTER_HOST:-}"
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
fi
if [[ -z "${ZENOH_ROUTER_HOST}" ]]; then
  ZENOH_ROUTER_HOST="127.0.0.1"
fi
ZENOH_ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT:-tcp/${ZENOH_ROUTER_HOST}:7447}"

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="${ZENOH_ROUTER_ENDPOINT}" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_pub.yml \
    --out-dir "${TMP_DIR}" >/dev/null

TYPE_HASH="$(python3 - "${TMP_DIR}/rmw_zenoh_pub_comm.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    data = json.load(f)
print(data["rmw_zenoh"]["mappings"][0]["ros2"]["type_hash"])
PY
)"

echo "Using std_msgs/msg/UInt64 type_hash=${TYPE_HASH}"
echo "Using Zenoh router endpoint=${ZENOH_ROUTER_ENDPOINT}"

ROUTER_PID=""
ROS_SUB_PID=""
ros2 run rmw_zenoh_cpp rmw_zenohd >"${ROUTER_LOG}" 2>&1 &
ROUTER_PID=$!
sleep "${HAKO_RMW_ZENOH_ROUTER_STARTUP_SEC:-1}"

python3 docker/ros/rmw_zenoh_uint64_sub.py >"${ROS_SUB_LOG}" 2>&1 &
ROS_SUB_PID=$!

cleanup() {
  if [[ -n "${ROS_SUB_PID}" ]]; then
    kill "${ROS_SUB_PID}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${ROUTER_PID}" ]]; then
    kill "${ROUTER_PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

sleep "${HAKO_RMW_ZENOH_SUBSCRIBER_STARTUP_SEC:-2}"
if ! kill -0 "${ROS_SUB_PID}" >/dev/null 2>&1; then
  wait "${ROS_SUB_PID}" || true
  echo "----- ROS subscriber log -----"
  cat "${ROS_SUB_LOG}"
  echo "----- rmw_zenohd log -----"
  cat "${ROUTER_LOG}"
  echo "ROS 2 subscriber exited before endpoint publishing started." >&2
  exit 1
fi

./build-docker/examples/endpoint_zenoh_pub_cdr "${ENDPOINT_CFG}" >"${ENDPOINT_LOG}" 2>&1
wait "${ROS_SUB_PID}" || true

echo "----- endpoint log -----"
cat "${ENDPOINT_LOG}"
echo "----- ROS subscriber log -----"
cat "${ROS_SUB_LOG}"
echo "----- rmw_zenohd log -----"
cat "${ROUTER_LOG}"

if ! grep -Eq 'received /sample_state=5' "${ROS_SUB_LOG}"; then
  echo "ROS 2 subscriber did not receive all endpoint rmw_zenoh CDR samples." >&2
  exit 1
fi
