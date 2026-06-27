# ROS 2 `rmw_zenoh` Docker Test

This Docker image installs a minimal ROS 2 `rmw_zenoh` environment. The source
tree is not copied into the image. Instead, `docker/run.bash` bind-mounts the
current `hakoniwa-pdu-endpoint` checkout into the container so local fixes are
available immediately.

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

Run the ROS publisher to endpoint subscriber smoke test inside the container:

```bash
bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

The test starts the `rmw_zenoh` router, generates a temporary endpoint config
with the ROS 2 `std_msgs/msg/UInt64` type hash, runs
`examples/endpoint_zenoh_sub`, then publishes `/sample_state` from a ROS 2 node.

If the ROS CLI cannot provide the type hash in your ROS distribution, pass it
explicitly:

```bash
RMW_ZENOH_TYPE_HASH=<std_msgs-msg-UInt64-type-hash> bash docker/run.bash \
  bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

You can also run a one-shot command from the host:

```bash
bash docker/run.bash bash tools/build-zenoh-docker.bash
bash docker/run.bash bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```

Environment overrides:

```bash
ROS_DISTRO=rolling \
HAKO_RMW_ZENOH_IMAGE=hako-pdu-rmw-zenoh \
HAKO_DOCKER_PLATFORM=linux/arm64 \
RMW_ZENOH_TYPE_HASH=<std_msgs-msg-UInt64-type-hash> \
  bash docker/run.bash bash docker/run_ros_rmw_zenoh_pub_to_endpoint_sub.bash
```
