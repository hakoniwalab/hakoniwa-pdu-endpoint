# Endpoint Tutorial (TCP, UDP, WebSocket)

This tutorial walks through install, build, run, and verify using TCP, then swaps the endpoint configuration to UDP without changing application code.
The key idea: you can swap transport protocols by editing JSON only, while keeping the same C++ application code and API calls.

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
- WebSocket server endpoint: `config/tutorial/endpoint_websocket_server.json`
- WebSocket client endpoint: `config/tutorial/endpoint_websocket_client.json`

Config roles and ports:
- TCP server listens on `0.0.0.0:9000` (in)
- TCP client connects to `127.0.0.1:9000` (out)
- UDP server listens on `0.0.0.0:9001` (in)
- UDP client sends to `127.0.0.1:9001` (out)
- WebSocket server listens on `0.0.0.0:9100` (in)
- WebSocket client connects to `ws://127.0.0.1:9100/` (out)

All of these reference `config/tutorial/cache/buffer.json` and the corresponding comm config.

## Sample Code

Use the sample app in `examples/endpoint_minimal.cpp`. It uses `PduResolvedKey` so the same code works for TCP and UDP.

The example is built by CMake when `HAKO_PDU_ENDPOINT_BUILD_EXAMPLES=ON` is set.

### Overview

This sample demonstrates a minimal endpoint application that can switch transports by configuration only. It opens a single endpoint, sends or receives a fixed PDU key, and prints payloads so you can verify end-to-end delivery.

### Sample code walkthrough

1. Parse CLI arguments (config path and mode) and create the endpoint:
   - Code: `examples/endpoint_minimal.cpp`
   - `Endpoint endpoint("tutorial_endpoint", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);`
2. Open and start the endpoint:
   - `endpoint.open(config_path)`
   - `endpoint.start()`
3. Define the fixed key used in this tutorial:
   - `PduResolvedKey key{"TutorialRobo", 0};`
4. Subscribe to receive callbacks (prints any received payload as text):
   - `endpoint.subscribe_on_recv_callback(...)`
5. In `send` mode:
   - Wait until the endpoint reports running.
   - Send `hello N` every 500ms via `endpoint.send(key, bytes)`.
6. In `recv` mode:
   - Sleep for the requested duration while callbacks print received data.

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
