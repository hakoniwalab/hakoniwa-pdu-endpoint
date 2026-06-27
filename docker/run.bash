#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="${HAKO_RMW_ZENOH_IMAGE:-hako-pdu-rmw-zenoh}"
ROS_DISTRO="${ROS_DISTRO:-rolling}"
CONTAINER_WORKDIR="/workspace/hakoniwa-pdu-endpoint"

bash "${SCRIPT_DIR}/create-image.bash"

mkdir -p "${ROOT_DIR}/.docker-home"

docker run --rm -it \
  --user "$(id -u):$(id -g)" \
  -e "HOME=${CONTAINER_WORKDIR}/.docker-home" \
  -e "ROS_DISTRO=${ROS_DISTRO}" \
  -e "RMW_IMPLEMENTATION=rmw_zenoh_cpp" \
  -e "RMW_ZENOH_TYPE_HASH" \
  -e "HAKO_RMW_ZENOH_TOPIC" \
  -e "HAKO_RMW_ZENOH_COUNT" \
  -e "HAKO_RMW_ZENOH_PERIOD" \
  -e "HAKO_RMW_ZENOH_TIMEOUT" \
  -e "HAKO_RMW_ZENOH_ALLOW_HASH_WILDCARD" \
  -e "HAKO_RMW_ZENOH_ROUTER_HOST" \
  -e "HAKO_RMW_ZENOH_ROUTER_ENDPOINT" \
  -e "HAKO_RMW_ZENOH_ROUTER_STARTUP_SEC" \
  -e "HAKO_RMW_ZENOH_SUBSCRIBER_STARTUP_SEC" \
  -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
  -w "${CONTAINER_WORKDIR}" \
  "${IMAGE_NAME}" \
  "${@:-bash}"
