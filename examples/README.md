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

Note: for larger systems, configs are expected to be generated and validated programmatically. See the validation section in the top-level README for how to run the schema validators.
