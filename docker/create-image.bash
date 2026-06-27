#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="${HAKO_RMW_ZENOH_IMAGE:-hako-pdu-rmw-zenoh}"
ROS_DISTRO="${ROS_DISTRO:-rolling}"
DOCKER_PLATFORM="${HAKO_DOCKER_PLATFORM:-}"

if [[ -z "${DOCKER_PLATFORM}" ]]; then
  case "$(uname -m)" in
    x86_64 | amd64)
      DOCKER_PLATFORM="linux/amd64"
      ;;
    arm64 | aarch64)
      DOCKER_PLATFORM="linux/arm64"
      ;;
    *)
      DOCKER_PLATFORM=""
      ;;
  esac
fi

build_args=(
  --build-arg "ROS_DISTRO=${ROS_DISTRO}"
  -f "${SCRIPT_DIR}/Dockerfile"
  -t "${IMAGE_NAME}"
)

if [[ -n "${DOCKER_PLATFORM}" ]]; then
  build_args+=(--platform "${DOCKER_PLATFORM}")
fi

docker build "${build_args[@]}" "$@" "${ROOT_DIR}"
