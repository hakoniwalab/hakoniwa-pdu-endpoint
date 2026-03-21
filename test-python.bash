#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_FIRST=${BUILD_FIRST:-ON}

PYTHON_TESTS=(
  "${PROJECT_ROOT}/python/test/test_c_endpoint_smoke.py"
  "${PROJECT_ROOT}/python/test/test_c_endpoint_callback_smoke.py"
  "${PROJECT_ROOT}/python/test/test_c_endpoint_ros_style_smoke.py"
  "${PROJECT_ROOT}/python/test/test_c_endpoint_recv_next_smoke.py"
  "${PROJECT_ROOT}/python/test/test_c_endpoint_pending_smoke.py"
  "${PROJECT_ROOT}/python/test/test_endpoint_container_smoke.py"
)

say() {
  printf "%s\n" "$*"
}

if [[ "${BUILD_FIRST}" == "ON" ]]; then
  "${PROJECT_ROOT}/build-python.bash"
fi

say "Running Python smoke tests..."
for test_script in "${PYTHON_TESTS[@]}"; do
  python3 "${test_script}"
done

say "Done."
