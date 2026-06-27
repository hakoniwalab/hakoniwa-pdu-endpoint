# ROS 2 `rmw_zenoh` Docker Test

This Docker image installs a minimal ROS 2 `rmw_zenoh` environment. The source
tree is not copied into the image. Instead, `docker/run.bash` bind-mounts the
current `hakoniwa-pdu-endpoint` checkout into the container so local fixes are
available immediately.

## Image

Create or update the image from the repository root:

```bash
bash docker/create-image.bash
```

`create-image.bash` detects the host architecture on macOS and Ubuntu and builds
the matching Linux image platform. Override it when needed:

```bash
HAKO_DOCKER_PLATFORM=linux/amd64 bash docker/create-image.bash
HAKO_DOCKER_PLATFORM=linux/arm64 bash docker/create-image.bash
```

Start the container:

```bash
bash docker/run.bash
```

Build inside the container:

```bash
bash tools/build-zenoh-docker.bash
```

## Config Generation

Generate a concrete endpoint config for manual endpoint execution:

```bash
bash tools/make-rmw-zenoh-config.bash \
  --recipe docker/recipes/rmw_zenoh_pub.yml \
  --out-dir /tmp/hako-rmw-zenoh-manual
./build-docker/examples/endpoint_zenoh_pub_cdr /tmp/hako-rmw-zenoh-manual/endpoint_rmw_zenoh_pub.json
```

Use `docker/recipes/rmw_zenoh_sub.yml` for subscriber-side endpoint configs.

The generator writes three files under `--out-dir`:

| direction | endpoint config | comm config | Zenoh config |
| --- | --- | --- | --- |
| `in` | `endpoint_rmw_zenoh_sub.json` | `rmw_zenoh_sub_comm.json` | `zenoh_client_sub.json5` |
| `out` | `endpoint_rmw_zenoh_pub.json` | `rmw_zenoh_pub_comm.json` | `zenoh_client_pub.json5` |
| `inout` | `endpoint_rmw_zenoh_pubsub.json` | `rmw_zenoh_pubsub_comm.json` | `zenoh_client_pubsub.json5` |

To resolve the ROS 2 type hash from registry metadata instead of the ROS
installation, pass the registry type-hash directory:

```bash
bash tools/make-rmw-zenoh-config.bash \
  --recipe config/sample/rmw_zenoh_recipe.yml \
  --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
  --out-dir /tmp/hako-rmw-zenoh-manual
```

For a two-topic bidirectional config, use the sample `inout` recipe:

```bash
bash tools/make-rmw-zenoh-config.bash \
  --recipe config/sample/rmw_zenoh_inout_recipe.yml \
  --type-hash-dir ../hakoniwa-pdu-registry/pdu/type_hash \
  --out-dir /tmp/hako-rmw-zenoh-inout
```

## Smoke Tests

Run the ROS publisher to endpoint subscriber smoke test inside the container:

```bash
bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

Expected endpoint-side output includes decoded CDR values:

```text
received sample_state_cdr=1 bytes=12
received sample_state_cdr=2 bytes=12
received sample_state_cdr=3 bytes=12
received sample_state_cdr=4 bytes=12
received sample_state_cdr=5 bytes=12
```

Run the reverse endpoint publisher to ROS subscriber smoke test:

```bash
bash docker/run_endpoint_rmw_zenoh_pub_to_ros_sub.bash
```

Expected ROS-side output includes:

```text
received /sample_state=1
received /sample_state=2
received /sample_state=3
received /sample_state=4
received /sample_state=5
```

Both smoke tests start the `rmw_zenoh` router and generate temporary endpoint
configs from `docker/recipes/rmw_zenoh_sub.yml` or
`docker/recipes/rmw_zenoh_pub.yml`. The generated config includes the concrete
`std_msgs/msg/UInt64` type hash. The image includes `ros2-type-hash`, a small
helper that reads ROS installed type-description JSON files.

## Overrides

The endpoint client config is generated at runtime and points to the container's
Zenoh router address. Override it when needed:

```bash
HAKO_RMW_ZENOH_ROUTER_ENDPOINT=tcp/172.17.0.2:7447 bash docker/run.bash \
  bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

You can pass the type hash explicitly:

```bash
RMW_ZENOH_TYPE_HASH=<std_msgs-msg-UInt64-type-hash> bash docker/run.bash \
  bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

For the endpoint publisher to ROS subscriber test, a concrete hash is required.

For receive-only smoke testing, a wildcard can be explicitly enabled if the hash
cannot be resolved:

```bash
HAKO_RMW_ZENOH_ALLOW_HASH_WILDCARD=1 bash docker/run.bash \
  bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

## One-Shot Host Commands

You can also run a one-shot command from the host:

```bash
bash docker/run.bash bash tools/build-zenoh-docker.bash
bash docker/run.bash bash tools/make-rmw-zenoh-config.bash \
  --recipe docker/recipes/rmw_zenoh_pub.yml \
  --out-dir /tmp/hako-rmw-zenoh-manual
bash docker/run.bash bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
bash docker/run.bash bash docker/run_endpoint_rmw_zenoh_pub_to_ros_sub.bash
```

Environment overrides:

```bash
ROS_DISTRO=rolling \
HAKO_RMW_ZENOH_IMAGE=hako-pdu-rmw-zenoh \
HAKO_DOCKER_PLATFORM=linux/arm64 \
RMW_ZENOH_TYPE_HASH=<std_msgs-msg-UInt64-type-hash> \
  bash docker/run.bash bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```
