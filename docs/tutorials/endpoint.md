# Endpoint Tutorial (TCP, UDP, WebSocket)

This tutorial walks through install, build, run, and verify using TCP, then swaps the endpoint configuration to UDP without changing application code.

## Prerequisites

- C++20 compiler and CMake
- Boost headers
- Hakoniwa core libraries (required for linking in this project)

Example on Ubuntu:
```bash
sudo apt-get update
sudo apt-get install -y g++-12 cmake libboost-dev git
```

Install hakoniwa-core-pro:
```bash
git clone https://github.com/hakoniwalab/hakoniwa-core-pro.git
cd hakoniwa-core-pro
bash build.bash
sudo bash install.bash
```

## Build the Library

From the repository root:
```bash
cmake -S . -B build -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON
cmake --build build
```

## Configuration Files

This tutorial uses the configs in `config/tutorial/`:

- TCP server endpoint: `config/tutorial/endpoint_tcp_server.json`
- TCP client endpoint: `config/tutorial/endpoint_tcp_client.json`
- UDP server endpoint: `config/tutorial/endpoint_udp_server.json`
- UDP client endpoint: `config/tutorial/endpoint_udp_client.json`

All of these reference `config/tutorial/cache/buffer.json` and the corresponding comm config.

## Sample Code

Use the sample app in `examples/endpoint_minimal.cpp`. It uses `PduResolvedKey` so the same code works for TCP and UDP.

The example is built by CMake when `HAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON` is set.

## Run (TCP)

Terminal 1 (server, receive):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_tcp_server.json recv 10
```

Terminal 2 (client, send):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_tcp_client.json send 5
```

Expected output:

- Client prints `sent: hello N`
- Server prints `recv: hello N`

## Swap to UDP (No Code Changes)

Use the UDP endpoint configs with the same binary:

Terminal 1 (server, receive):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_udp_server.json recv 10
```

Terminal 2 (client, send):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_udp_client.json send 5
```

If you see the same `sent:`/`recv:` logs, the swap worked.

## Swap to WebSocket (No Code Changes)

Use the WebSocket endpoint configs with the same binary:

Terminal 1 (server, receive):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_websocket_server.json recv 10
```

Terminal 2 (client, send):
```bash
build/examples/endpoint_minimal config/tutorial/endpoint_websocket_client.json send 5
```

Expected output:

- Client prints `sent: hello N`
- Server prints `recv: hello N`

Note: when the client exits, the server may log a read error as the connection closes.
