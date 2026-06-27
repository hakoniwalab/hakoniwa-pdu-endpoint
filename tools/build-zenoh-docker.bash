#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HAKO_PDU_ENDPOINT_DOCKER_BUILD_DIR:-build-docker}"

source "/opt/ros/${ROS_DISTRO:-rolling}/setup.bash"

cd "${ROOT_DIR}"

cmake -S . -B "${BUILD_DIR}" \
  -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON \
  -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF \
  -DHAKO_PDU_ENDPOINT_INSTALL=OFF

cmake --build "${BUILD_DIR}" -j"$(nproc)"
