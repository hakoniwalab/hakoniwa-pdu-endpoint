#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${PROJECT_ROOT}/build-shared"}

BUILD_EXAMPLES=${BUILD_EXAMPLES:-ON}
BUILD_TESTS=${BUILD_TESTS:-ON}

LIB_CSPROJ="${PROJECT_ROOT}/csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj"
TEST_CSPROJ="${PROJECT_ROOT}/csharp/tests/SmokeTests/SmokeTests.csproj"

EXAMPLE_PROJECTS=(
  "${PROJECT_ROOT}/csharp/examples/MinimalExample/MinimalExample.csproj"
  "${PROJECT_ROOT}/csharp/examples/ManualPumpExample/ManualPumpExample.csproj"
  "${PROJECT_ROOT}/csharp/examples/RecvNextExample/RecvNextExample.csproj"
)

say() {
  printf "%s\n" "$*"
}

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  say "Configuring shared native library build..."
  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DBUILD_SHARED_LIBS=ON
fi

say "Building shared native library..."
cmake --build "${BUILD_DIR}" --target hakoniwa_pdu_endpoint

say "Building C# binding library..."
dotnet build "${LIB_CSPROJ}"

if [[ "${BUILD_TESTS}" == "ON" ]]; then
  say "Building C# smoke tests..."
  dotnet build "${TEST_CSPROJ}"
fi

if [[ "${BUILD_EXAMPLES}" == "ON" ]]; then
  say "Building C# examples..."
  for csproj in "${EXAMPLE_PROJECTS[@]}"; do
    dotnet build "${csproj}"
  done
fi

say "Done."
