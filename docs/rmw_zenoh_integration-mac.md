# `rmw_zenoh` macOS Runbook

This runbook lists only commands that are run on macOS. Use it together with
`docs/rmw_zenoh_integration-ubuntu.md` for two-machine experiments.

The examples use:

- ROS 2 topic: `/sample_state`
- ROS 2 type: `std_msgs/msg/UInt64`
- Hakoniwa endpoint: `StorageDemo/sample_state`
- Ubuntu Zenoh router endpoint: `tcp/<ubuntu-ip-address>:7447`

## Installer Setup

Run the installer once on macOS:

```bash
./tools/install-rmw-zenoh.bash \
  --router-endpoint tcp/<ubuntu-ip-address>:7447

source rmw-zenoh.env
```

The installer builds the C++ CDR examples, builds the shared library for the
Python examples, and generates `rmw-zenoh.env`. Re-run the installer when the
router address or build options change.

After sourcing `rmw-zenoh.env`, create endpoint configs with short helper
commands:

```bash
hako_rmw_zenoh_make_pub_config /tmp/hako-rmw-zenoh-pub
hako_rmw_zenoh_make_sub_config /tmp/hako-rmw-zenoh-sub
```

Run C++ examples:

```bash
hako_rmw_zenoh_cpp_pub /tmp/hako-rmw-zenoh-pub/endpoint_rmw_zenoh_pub.json
hako_rmw_zenoh_cpp_sub /tmp/hako-rmw-zenoh-sub/endpoint_rmw_zenoh_sub.json
```

Run Python examples:

```bash
hako_rmw_zenoh_python_pub /tmp/hako-rmw-zenoh-pub/endpoint_rmw_zenoh_pub.json
hako_rmw_zenoh_python_sub /tmp/hako-rmw-zenoh-sub/endpoint_rmw_zenoh_sub.json
```

To install Homebrew dependencies at the same time, add `--install-deps`.

## Native Build

Install dependencies:

```bash
brew install cmake boost googletest rust
brew install px4/px4/fastcdr
```

Build C++ examples with Zenoh enabled:

```bash
cmake -S . -B build-zenoh \
  -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON \
  -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF \
  -DHAKO_PDU_ENDPOINT_INSTALL=OFF \
  -DCMAKE_PREFIX_PATH="$(brew --prefix fastcdr)"

cmake --build build-zenoh -j4

test -x ./build-zenoh/examples/endpoint_zenoh_pub_cdr
test -x ./build-zenoh/examples/endpoint_zenoh_sub_cdr
```

## Mac Endpoint Pub To Ubuntu ROS 2 Echo

Start `rmw_zenohd` and `ros2 topic echo` on Ubuntu first. Then run this on
macOS:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_pub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-macos-to-ubuntu

./build-zenoh/examples/endpoint_zenoh_pub_cdr \
  /tmp/hako-rmw-zenoh-macos-to-ubuntu/endpoint_rmw_zenoh_pub.json
```

Do not use `endpoint_zenoh_pub` for ROS 2 echo tests. It sends raw endpoint
bytes, not ROS 2 CDR bytes.

## Ubuntu ROS 2 Pub To Mac Endpoint Sub

Start `rmw_zenohd` on Ubuntu first. Then run this on macOS:

```bash
UBUNTU_IP=<ubuntu-ip-address>

HAKO_RMW_ZENOH_ROUTER_ENDPOINT="tcp/${UBUNTU_IP}:7447" \
  bash tools/make-rmw-zenoh-config.bash \
    --recipe docker/recipes/rmw_zenoh_sub.yml \
    --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
    --out-dir /tmp/hako-rmw-zenoh-ubuntu-to-macos

./build-zenoh/examples/endpoint_zenoh_sub_cdr \
  /tmp/hako-rmw-zenoh-ubuntu-to-macos/endpoint_rmw_zenoh_sub.json
```

Expected output starts with:

```text
Waiting for Zenoh CDR samples...
```

Publish from Ubuntu while this subscriber is running.

## Endpoint Pub/Sub Between Mac And Linux

Use Ubuntu `rmw_zenohd` as the router. On the receiving machine, run:

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

On the publishing machine, run:

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

Swap publisher and subscriber machines to test the opposite direction.

## Python Endpoint

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

cmake --build build-zenoh-shared -j4

BUILD_DIR="$(pwd)/build-zenoh-shared" bash build-python.bash
```

Set Python runtime paths:

```bash
export PYTHONPATH="$(pwd)/python:$(pwd)/build-zenoh-shared/python"
export HAKO_PDU_ENDPOINT_LIB_DIR="$(pwd)/build-zenoh-shared/src"
export HAKO_PDU_ENDPOINT_SHARED_LIB="$(pwd)/build-zenoh-shared/src/libhakoniwa_pdu_endpoint.dylib"
export HAKO_PDU_REGISTRY_PDU_PATH="$(pwd)/../hakoniwa-pdu-registry/pdu"
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

The Python examples use the generated CDR converters under
`../hakoniwa-pdu-registry/pdu/python`. Override the registry location with
`HAKO_PDU_REGISTRY_PDU_PATH` when needed.
