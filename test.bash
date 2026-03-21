#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build"}
TEST_BIN=${TEST_BIN:-"${BUILD_DIR}/test/endpoint_test"}
GTEST_FILTER=${GTEST_FILTER:-}

say() {
  printf "%s\n" "$*"
}

if [[ ! -x "${TEST_BIN}" ]]; then
  say "Test binary not found: ${TEST_BIN}"
  say "Building endpoint_test..."
  cmake --build "${BUILD_DIR}" --target endpoint_test
fi

ARGS=("${TEST_BIN}" "--gtest_color=no")
if [[ -n "${GTEST_FILTER}" ]]; then
  ARGS+=("--gtest_filter=${GTEST_FILTER}")
fi

say "Running tests..."
"${ARGS[@]}"
