# Examples

These examples are intentionally small and test-friendly. They are NOT step-by-step tutorials; they are executable reference configurations. If you want to unit-test your application logic without any network dependency, use the Internal Cache example (`comm: null`) as a template.

Build examples with CMake:

```bash
cmake -S . -B build -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON
cmake --build build
```

## TCP (inout)

- `endpoint_tcp_server`
  - Config: `config/sample/endpoint_tcp_server.json`
- `endpoint_tcp_client`
  - Config: `config/sample/endpoint_tcp_client.json`

What this demonstrates: explicit connection lifecycle and stream framing via TCP inout.

Run in two terminals:

```bash
./build/examples/endpoint_tcp_server
```

```bash
./build/examples/endpoint_tcp_client
```

## UDP (one-way)

- `endpoint_udp_server`
  - Config: `config/tutorial/endpoint_udp_server.json`
- `endpoint_udp_client`
  - Config: `config/tutorial/endpoint_udp_client.json`

What this demonstrates: connectionless delivery with direction defined by config.

Run in two terminals:

```bash
./build/examples/endpoint_udp_server
```

```bash
./build/examples/endpoint_udp_client
```

## WebSocket (inout)

- `endpoint_ws_server`
  - Config: `config/sample/endpoint_websocket_server.json`
- `endpoint_ws_client`
  - Config: `config/sample/endpoint_websocket_client.json`

What this demonstrates: the same Endpoint semantics carried over WebSocket transport.

Run in two terminals:

```bash
./build/examples/endpoint_ws_server
```

```bash
./build/examples/endpoint_ws_client
```

## TCP Mux

- `endpoint_tcp_mux`
  - Config: `config/sample/endpoint_mux.json`

What this demonstrates: a single server accepts multiple connections, gated by `expected_clients`.

This example waits until `expected_clients` connections are established, then it reports the number of ready endpoints.

```bash
./build/examples/endpoint_tcp_mux
```

## Internal Cache (No Comm)

This is useful for fast unit tests or app-level logic tests without any network dependency.

- `endpoint_internal_cache`
  - Config: `config/sample/endpoint_internal_cache.json`

What this demonstrates: `comm: null` enables network-free unit/integration testing.

```bash
./build/examples/endpoint_internal_cache
```

## Storage Queue

- `endpoint_storage_queue`
  - Config: `config/sample/endpoint_storage_queue.json`

What this demonstrates: append-only storage logging for replay-style inspection.

```bash
./build/examples/endpoint_storage_queue
build/tools/hako_pdu_storage_debug config/runtime/storage_queue.bin
```

If you want machine-readable metadata:

```bash
build/tools/hako_pdu_storage_debug config/runtime/storage_queue.bin --json
```

## Storage Latest

- `endpoint_storage_latest`
  - Config: `config/sample/endpoint_storage_latest.json`

What this demonstrates: fixed-slot snapshot storage where one key keeps only its latest packet.

```bash
./build/examples/endpoint_storage_latest
build/tools/hako_pdu_storage_debug config/runtime/storage_latest.bin
```

If you want machine-readable metadata:

```bash
build/tools/hako_pdu_storage_debug config/runtime/storage_latest.bin --json
```

## Zenoh Pub/Sub

- `endpoint_zenoh_pub`
  - Config: `config/sample/endpoint_zenoh_pub.json`
- `endpoint_zenoh_sub`
  - Config: `config/sample/endpoint_zenoh_sub.json`

What this demonstrates: minimal Zenoh transport integration using `PduResolvedKey -> <key_prefix>/<robot>/<channel_id>`.

Prerequisite:

- build with `-DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON`
- fetched `zenoh-c` version is pinned by `ZENOH_VERSION.txt`
- no `zenohd` is required for the sample pair
- subscriber uses `config/sample/comm/zenoh/peer_listen.json5`
- publisher uses `config/sample/comm/zenoh/peer_connect.json5`
- router sample is also available at `config/sample/comm/zenoh/router.json5`

Run in two terminals:

```bash
./build/examples/endpoint_zenoh_sub
```

```bash
./build/examples/endpoint_zenoh_pub
```

Both examples also accept an endpoint config path as the first argument:

```bash
./build/examples/endpoint_zenoh_sub config/sample/endpoint_zenoh_sub.json
./build/examples/endpoint_zenoh_pub config/sample/endpoint_zenoh_pub.json
```

The same binaries can be pointed at the `rmw_zenoh` sample configs for
Hakoniwa endpoint-to-endpoint smoke testing:

```bash
./build/examples/endpoint_zenoh_sub config/sample/endpoint_rmw_zenoh_sub.json
./build/examples/endpoint_zenoh_pub config/sample/endpoint_rmw_zenoh_pub.json
```

These `rmw_zenoh` sample configs use a placeholder `type_hash` for endpoint-to-endpoint testing. Replace it with registry-managed ROS 2 type-hash metadata before testing against real ROS 2 `rmw_zenoh` nodes.

### CDR Examples

When Fast CDR is available, the examples build CDR-aware variants:

- `endpoint_zenoh_pub_cdr`
- `endpoint_zenoh_sub_cdr`

These examples keep the endpoint transport binary-only. The application layer
uses the generated `std_msgs/UInt64` CDR converter copied under `examples/cdr`.
The Docker `rmw_zenoh` smoke tests use these binaries for ROS 2 interop.

```bash
./build/examples/endpoint_zenoh_sub_cdr config/sample/endpoint_rmw_zenoh_sub.json
./build/examples/endpoint_zenoh_pub_cdr config/sample/endpoint_rmw_zenoh_pub.json
```

## MQTT Pub/Sub

- `endpoint_mqtt_pub`
  - Config: `config/sample/endpoint_mqtt_pub.json`
- `endpoint_mqtt_sub`
  - Config: `config/sample/endpoint_mqtt_sub.json`

What this demonstrates: minimal MQTT transport integration using topic mapping
`<topic_prefix>/<robot>/<channel_id>` and callback-driven receive delivery.

Prerequisite:

- build with `-DHAKO_PDU_ENDPOINT_ENABLE_MQTT=ON`
- fetched `paho.mqtt.cpp` version is pinned by `MQTT_VERSION.txt`
- a local broker is required; the sample commands below use `mosquitto`

Build with MQTT enabled:

```bash
cmake -S . -B build-mqtt \
  -DHAKO_PDU_ENDPOINT_ENABLE_MQTT=ON \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON
cmake --build build-mqtt -j4
```

Run in three terminals:

Terminal 1:

```bash
mosquitto -p 1883
```

Terminal 2:

```bash
./build-mqtt/examples/endpoint_mqtt_sub
```

Terminal 3:

```bash
./build-mqtt/examples/endpoint_mqtt_pub
```

Expected subscriber output:

```text
Waiting for MQTT samples...
received sample_state=1
received sample_state=2
received sample_state=3
received sample_state=4
received sample_state=5
```

## Python Runtime Access

These Python examples sit on top of the C facade and `cffi` wrapper. Build the
core library first, then generate the Python extension module:

```bash
cmake -S . -B build
cmake --build build -j4
python3 python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py
```

- `python/examples/endpoint_internal_cache.py`
  - Thin `Endpoint` wrapper over the C facade
- `python/examples/endpoint_callback.py`
  - Callback convenience through `Endpoint.start_dispatch()`
- `python/examples/endpoint_recv_next.py`
  - Runtime `recv_next(...)` for internal `latest` and `queue` cache semantics
- `python/examples/endpoint_container.py`
  - Pure-Python `EndpointContainer` built by composing wrapped endpoints

Run:

```bash
python3 python/examples/endpoint_internal_cache.py
python3 python/examples/endpoint_callback.py
python3 python/examples/endpoint_recv_next.py
python3 python/examples/endpoint_container.py
```

Note: for larger systems, configs are expected to be generated and validated programmatically. See the validation section in the top-level README for how to run the schema validators.
If you are generating many configs, see the config generator in the top-level README.
