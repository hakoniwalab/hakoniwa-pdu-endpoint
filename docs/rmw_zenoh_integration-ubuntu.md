# `rmw_zenoh` Ubuntu/Linux Runbook

This runbook lists only commands that are run on Ubuntu or Linux. Use it
together with `docs/rmw_zenoh_integration-mac.md` for two-machine experiments.

The examples use:

- ROS 2 topic: `/sample_state`
- ROS 2 type: `std_msgs/msg/UInt64`
- Hakoniwa endpoint: `StorageDemo/sample_state`
- Zenoh router port: `7447`

## ROS 2 `rmw_zenoh` Install

Set `ROS_DISTRO` to the installed ROS 2 distro, for example `rolling` or
`jazzy`:

```bash
export ROS_DISTRO=rolling

sudo apt-get update
sudo apt-get install -y \
  ros-${ROS_DISTRO}-rmw-zenoh-cpp \
  ros-${ROS_DISTRO}-ros2cli \
  ros-${ROS_DISTRO}-ros2interface \
  ros-${ROS_DISTRO}-std-msgs
```

Verify that `rmw_zenohd` is available:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp

ros2 pkg executables rmw_zenoh_cpp
```

The output should include:

```text
rmw_zenoh_cpp rmw_zenohd
```

## Start `rmw_zenohd`

Run this in a dedicated terminal and keep it running:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp

ros2 run rmw_zenoh_cpp rmw_zenohd
```

The log should show a reachable endpoint such as:

```text
Zenoh can be reached at: tcp/192.168.x.y:7447
```

Use that IP address as `UBUNTU_IP` on the other machine.

## Echo Mac Endpoint Publisher

After starting `rmw_zenohd`, run this in another Ubuntu terminal:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp

ros2 topic echo /sample_state std_msgs/msg/UInt64
```

Start the Mac endpoint publisher while this echo command is running.

## Publish To Mac Endpoint Subscriber

After starting `rmw_zenohd`, run this while the Mac endpoint subscriber is
waiting:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp

ros2 topic pub --rate 1 /sample_state std_msgs/msg/UInt64 "{data: 1}"
```

Stop with `Ctrl-C` after the Mac endpoint receives samples.

## Linux Endpoint Native Build

Install native dependencies:

```bash
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
  rustc
```

Build C++ examples with Zenoh enabled:

```bash
cmake -S . -B build-zenoh \
  -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON \
  -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF \
  -DHAKO_PDU_ENDPOINT_INSTALL=OFF

cmake --build build-zenoh -j"$(nproc)"

test -x ./build-zenoh/examples/endpoint_zenoh_pub_cdr
test -x ./build-zenoh/examples/endpoint_zenoh_sub_cdr
```

## Linux Endpoint Pub/Sub

Use Ubuntu `rmw_zenohd` as the router. On the receiving Linux machine:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_sub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-endpoint-sub

./build-zenoh/examples/endpoint_zenoh_sub_cdr \
  /tmp/hako-rmw-zenoh-endpoint-sub/endpoint_rmw_zenoh_sub.json
```

On the publishing Linux machine:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_pub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-endpoint-pub

./build-zenoh/examples/endpoint_zenoh_pub_cdr \
  /tmp/hako-rmw-zenoh-endpoint-pub/endpoint_rmw_zenoh_pub.json
```

## Python Endpoint On Linux

Install the native dependencies from `Linux Endpoint Native Build` first. The
Python binding build requires `python3-cffi` and `python3-dev`.

Build the shared native library and Python CFFI module:

```bash
cmake -S . -B build-zenoh-shared \
  -DBUILD_SHARED_LIBS=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF \
  -DHAKO_PDU_ENDPOINT_INSTALL=OFF

cmake --build build-zenoh-shared -j"$(nproc)"

BUILD_DIR="$(pwd)/build-zenoh-shared" bash build-python.bash
```

Set Python runtime paths:

```bash
export PYTHONPATH="$(pwd)/python:$(pwd)/build-zenoh-shared/python"
export HAKO_PDU_ENDPOINT_LIB_DIR="$(pwd)/build-zenoh-shared/src"
export HAKO_PDU_ENDPOINT_SHARED_LIB="$(pwd)/build-zenoh-shared/src/libhakoniwa_pdu_endpoint.so"
export HAKO_PDU_REGISTRY_PDU_PATH="$(pwd)/../hakoniwa-pdu-registry/pdu"
export LD_LIBRARY_PATH="$(pwd)/build-zenoh-shared/src:${LD_LIBRARY_PATH:-}"
```

Python subscriber:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_sub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-python-sub

python3 python/examples/endpoint_rmw_zenoh_sub_cdr.py \
  /tmp/hako-rmw-zenoh-python-sub/endpoint_rmw_zenoh_sub.json
```

Python publisher:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_pub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-python-pub

python3 python/examples/endpoint_rmw_zenoh_pub_cdr.py \
  /tmp/hako-rmw-zenoh-python-pub/endpoint_rmw_zenoh_pub.json
```
