#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build-shared"}
CSPROJ=${CSPROJ:-"${PROJECT_ROOT}/csharp/tests/SmokeTests/SmokeTests.csproj"}

say() {
  printf "%s\n" "$*"
}

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  say "Configuring shared native library build..."
  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DBUILD_SHARED_LIBS=ON
fi

say "Building shared native library..."
cmake --build "${BUILD_DIR}" --target hakoniwa_pdu_endpoint

say "Building C# smoke tests..."
dotnet build "${CSPROJ}"

LIB_ENV_NAME="LD_LIBRARY_PATH"
if [[ "$(uname -s)" == "Darwin" ]]; then
  LIB_ENV_NAME="DYLD_LIBRARY_PATH"
fi

EXISTING_VALUE="${!LIB_ENV_NAME-}"
LIB_DIR="${BUILD_DIR}/src"
if [[ -n "${EXISTING_VALUE}" ]]; then
  export "${LIB_ENV_NAME}=${LIB_DIR}:${EXISTING_VALUE}"
else
  export "${LIB_ENV_NAME}=${LIB_DIR}"
fi

say "Running C# smoke tests..."
dotnet run --project "${CSPROJ}" --no-build
