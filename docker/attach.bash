#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${HAKO_RMW_ZENOH_IMAGE:-hako-pdu-rmw-zenoh}"
ROS_DISTRO="${ROS_DISTRO:-rolling}"

DOCKER_ID="$(docker ps --filter "ancestor=${IMAGE_NAME}" --format '{{.ID}}' | head -n 1)"

if [[ -z "${DOCKER_ID}" ]]; then
  echo "No running container found for image: ${IMAGE_NAME}" >&2
  echo "Start one first: bash docker/run.bash" >&2
  exit 1
fi

docker exec -it \
  -e "ROS_DISTRO=${ROS_DISTRO}" \
  -e "RMW_IMPLEMENTATION=rmw_zenoh_cpp" \
  "${DOCKER_ID}" \
  /bin/bash -lc "source /opt/ros/${ROS_DISTRO}/setup.bash && cd /workspace/hakoniwa-pdu-endpoint && exec /bin/bash"
