#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build-zenoh"
SHARED_BUILD_DIR="${ROOT_DIR}/build-zenoh-shared"
ENV_FILE="${ROOT_DIR}/rmw-zenoh.env"
REGISTRY_PDU_PATH="${ROOT_DIR}/../hakoniwa-pdu-registry/pdu"
ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT:-}"
ROUTER_HOST="${HAKO_RMW_ZENOH_ROUTER_HOST:-}"
ROS_DISTRO="${ROS_DISTRO:-rolling}"
INSTALL_DEPS=0
BUILD_CPP=1
BUILD_PYTHON=1

usage() {
  cat <<'USAGE'
usage: bash tools/install-rmw-zenoh.bash [options]

Build the rmw_zenoh-enabled endpoint artifacts and generate rmw-zenoh.env.
After installation, run:

  source rmw-zenoh.env

Options:
  --env-file <path>             Output env file. Default: ./rmw-zenoh.env
  --build-dir <path>            C++ example build dir. Default: ./build-zenoh
  --shared-build-dir <path>     Shared library build dir. Default: ./build-zenoh-shared
  --registry-pdu-path <path>    hakoniwa-pdu-registry/pdu path.
                                Default: ../hakoniwa-pdu-registry/pdu
  --router-endpoint <endpoint>  Zenoh router endpoint, e.g. tcp/192.168.2.121:7447
  --router-host <host>          Router host fallback. Used only when endpoint is omitted.
  --ros-distro <name>           ROS 2 distro for Ubuntu env. Default: ${ROS_DISTRO:-rolling}
  --install-deps                Install OS packages with brew or apt-get.
  --skip-cpp-build              Do not build C++ examples.
  --skip-python-build           Do not build shared library or Python CFFI module.
  -h, --help                    Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env-file)
      ENV_FILE="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --shared-build-dir)
      SHARED_BUILD_DIR="$2"
      shift 2
      ;;
    --registry-pdu-path)
      REGISTRY_PDU_PATH="$2"
      shift 2
      ;;
    --router-endpoint)
      ROUTER_ENDPOINT="$2"
      shift 2
      ;;
    --router-host)
      ROUTER_HOST="$2"
      shift 2
      ;;
    --ros-distro)
      ROS_DISTRO="$2"
      shift 2
      ;;
    --install-deps)
      INSTALL_DEPS=1
      shift
      ;;
    --skip-cpp-build)
      BUILD_CPP=0
      shift
      ;;
    --skip-python-build)
      BUILD_PYTHON=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$(uname -s)" in
  Darwin)
    OS_NAME="macos"
    SHARED_LIB_NAME="libhakoniwa_pdu_endpoint.dylib"
    LIB_PATH_VAR="DYLD_LIBRARY_PATH"
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    ;;
  Linux)
    OS_NAME="linux"
    SHARED_LIB_NAME="libhakoniwa_pdu_endpoint.so"
    LIB_PATH_VAR="LD_LIBRARY_PATH"
    JOBS="$(nproc 2>/dev/null || echo 4)"
    ;;
  *)
    echo "Unsupported OS: $(uname -s)" >&2
    exit 2
    ;;
esac

if [[ -z "${ROUTER_ENDPOINT}" ]]; then
  if [[ -z "${ROUTER_HOST}" ]]; then
    if [[ "${OS_NAME}" == "linux" ]]; then
      ROUTER_HOST="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
    fi
  fi
  ROUTER_HOST="${ROUTER_HOST:-127.0.0.1}"
  ROUTER_ENDPOINT="tcp/${ROUTER_HOST}:7447"
fi

install_deps() {
  if [[ "${OS_NAME}" == "macos" ]]; then
    command -v brew >/dev/null 2>&1 || {
      echo "Homebrew is required for --install-deps on macOS." >&2
      exit 1
    }
    brew install cmake boost googletest rust
    brew install px4/px4/fastcdr
  else
    sudo apt-get update
    sudo apt-get install -y \
      build-essential \
      cargo \
      cmake \
      git \
      libboost-dev \
      libfastcdr-dev \
      pkg-config \
      python3 \
      python3-cffi \
      python3-dev \
      rustc \
      "ros-${ROS_DISTRO}-rmw-zenoh-cpp" \
      "ros-${ROS_DISTRO}-ros2cli" \
      "ros-${ROS_DISTRO}-ros2interface" \
      "ros-${ROS_DISTRO}-std-msgs"
  fi
}

if [[ "${INSTALL_DEPS}" -eq 1 ]]; then
  install_deps
fi

if [[ "${BUILD_CPP}" -eq 1 ]]; then
  CMAKE_ARGS=(
    -S "${ROOT_DIR}"
    -B "${BUILD_DIR}"
    -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON
    -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF
    -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON
    -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF
    -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF
    -DHAKO_PDU_ENDPOINT_INSTALL=OFF
  )
  if [[ "${OS_NAME}" == "macos" ]] && command -v brew >/dev/null 2>&1; then
    if brew --prefix fastcdr >/dev/null 2>&1; then
      CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$(brew --prefix fastcdr)")
    fi
  fi
  cmake "${CMAKE_ARGS[@]}"
  cmake --build "${BUILD_DIR}" -j"${JOBS}"
fi

if [[ "${BUILD_PYTHON}" -eq 1 ]]; then
  CMAKE_ARGS=(
    -S "${ROOT_DIR}"
    -B "${SHARED_BUILD_DIR}"
    -DBUILD_SHARED_LIBS=ON
    -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON
    -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF
    -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=OFF
    -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF
    -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF
    -DHAKO_PDU_ENDPOINT_INSTALL=OFF
  )
  if [[ "${OS_NAME}" == "macos" ]] && command -v brew >/dev/null 2>&1; then
    if brew --prefix fastcdr >/dev/null 2>&1; then
      CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$(brew --prefix fastcdr)")
    fi
  fi
  cmake "${CMAKE_ARGS[@]}"
  cmake --build "${SHARED_BUILD_DIR}" -j"${JOBS}"
  BUILD_DIR="${SHARED_BUILD_DIR}" bash "${ROOT_DIR}/build-python.bash"
fi

mkdir -p "$(dirname "${ENV_FILE}")"

cat > "${ENV_FILE}" <<EOF
# Generated by tools/install-rmw-zenoh.bash.
# Source this file from bash or zsh:
#   source ${ENV_FILE}

export HAKO_PDU_ENDPOINT_ROOT="${ROOT_DIR}"
export HAKO_PDU_ENDPOINT_BUILD_DIR="${BUILD_DIR}"
export HAKO_PDU_ENDPOINT_SHARED_BUILD_DIR="${SHARED_BUILD_DIR}"
export HAKO_PDU_ENDPOINT_PYTHON_BUILD_DIR="${SHARED_BUILD_DIR}/python"
export HAKO_PDU_ENDPOINT_LIB_DIR="${SHARED_BUILD_DIR}/src"
export HAKO_PDU_ENDPOINT_SHARED_LIB="${SHARED_BUILD_DIR}/src/${SHARED_LIB_NAME}"
export HAKO_PDU_REGISTRY_PDU_PATH="${REGISTRY_PDU_PATH}"
export HAKO_RMW_ZENOH_TYPE_HASH_DIR="${REGISTRY_PDU_PATH}/type_hash"
export HAKO_RMW_ZENOH_ROUTER_ENDPOINT="${ROUTER_ENDPOINT}"

case ":\${PYTHONPATH:-}:" in
  *:"${ROOT_DIR}/python":*) ;;
  *) export PYTHONPATH="${ROOT_DIR}/python:\${PYTHONPATH:-}" ;;
esac
case ":\${PYTHONPATH:-}:" in
  *:"${SHARED_BUILD_DIR}/python":*) ;;
  *) export PYTHONPATH="${SHARED_BUILD_DIR}/python:\${PYTHONPATH:-}" ;;
esac
case ":\${${LIB_PATH_VAR}:-}:" in
  *:"${SHARED_BUILD_DIR}/src":*) ;;
  *) export ${LIB_PATH_VAR}="${SHARED_BUILD_DIR}/src:\${${LIB_PATH_VAR}:-}" ;;
esac
EOF

if [[ "${OS_NAME}" == "linux" ]]; then
  cat >> "${ENV_FILE}" <<EOF

export ROS_DISTRO="${ROS_DISTRO}"
if [ -f "/opt/ros/\${ROS_DISTRO}/setup.bash" ]; then
  source "/opt/ros/\${ROS_DISTRO}/setup.bash"
fi
export RMW_IMPLEMENTATION="rmw_zenoh_cpp"
EOF
fi

cat >> "${ENV_FILE}" <<'EOF'

hako_rmw_zenoh_make_pub_config() {
  local out_dir="${1:-/tmp/hako-rmw-zenoh-pub}"
  HAKO_RMW_ZENOH_ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT}" \
    bash "${HAKO_PDU_ENDPOINT_ROOT}/tools/make-rmw-zenoh-config.bash" \
      --recipe "${HAKO_PDU_ENDPOINT_ROOT}/docker/recipes/rmw_zenoh_pub.yml" \
      --type-hash-dir "${HAKO_RMW_ZENOH_TYPE_HASH_DIR}" \
      --out-dir "${out_dir}"
}

hako_rmw_zenoh_make_sub_config() {
  local out_dir="${1:-/tmp/hako-rmw-zenoh-sub}"
  HAKO_RMW_ZENOH_ROUTER_ENDPOINT="${HAKO_RMW_ZENOH_ROUTER_ENDPOINT}" \
    bash "${HAKO_PDU_ENDPOINT_ROOT}/tools/make-rmw-zenoh-config.bash" \
      --recipe "${HAKO_PDU_ENDPOINT_ROOT}/docker/recipes/rmw_zenoh_sub.yml" \
      --type-hash-dir "${HAKO_RMW_ZENOH_TYPE_HASH_DIR}" \
      --out-dir "${out_dir}"
}

hako_rmw_zenoh_cpp_pub() {
  local config="${1:-/tmp/hako-rmw-zenoh-pub/endpoint_rmw_zenoh_pub.json}"
  "${HAKO_PDU_ENDPOINT_BUILD_DIR}/examples/endpoint_zenoh_pub_cdr" "${config}"
}

hako_rmw_zenoh_cpp_sub() {
  local config="${1:-/tmp/hako-rmw-zenoh-sub/endpoint_rmw_zenoh_sub.json}"
  "${HAKO_PDU_ENDPOINT_BUILD_DIR}/examples/endpoint_zenoh_sub_cdr" "${config}"
}

hako_rmw_zenoh_python_pub() {
  local config="${1:-/tmp/hako-rmw-zenoh-pub/endpoint_rmw_zenoh_pub.json}"
  python3 "${HAKO_PDU_ENDPOINT_ROOT}/python/examples/endpoint_rmw_zenoh_pub_cdr.py" "${config}"
}

hako_rmw_zenoh_python_sub() {
  local config="${1:-/tmp/hako-rmw-zenoh-sub/endpoint_rmw_zenoh_sub.json}"
  python3 "${HAKO_PDU_ENDPOINT_ROOT}/python/examples/endpoint_rmw_zenoh_sub_cdr.py" "${config}"
}
EOF

echo "Generated: ${ENV_FILE}"
echo "Router endpoint: ${ROUTER_ENDPOINT}"
echo "Next:"
echo "  source ${ENV_FILE}"
