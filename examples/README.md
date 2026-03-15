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

Note: for larger systems, configs are expected to be generated and validated programmatically. See the validation section in the top-level README for how to run the schema validators.
If you are generating many configs, see the config generator in the top-level README.
