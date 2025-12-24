# hakoniwa-pdu-endpoint

`hakoniwa-pdu-endpoint` is a C++ library that provides a file-based configuration for UDP/TCP communication endpoints, designed for Hakoniwa PDU (Protocol Data Unit) communication. It allows for flexible setup of sockets by defining communication parameters in a simple JSON file.

## Features

-   **JSON-based Endpoint Configuration**: Both Raw Endpoints (for network communication) and Smart Endpoints (for in-memory PDU processing) are easily configured via JSON files, allowing detailed setup of communication parameters and internal logic.
-   **Flexible PDU Handling**: Supports various PDU handling strategies, including direct network communication (UDP/TCP) through Raw Endpoints and advanced in-memory management (e.g., state caching, event queuing) via Smart Endpoints.
-   **Multiple Communication Directions**: Supports `in` (receive-only), `out` (send-only), and `inout` (bidirectional) communication modes.
-   **Dynamic Replies**: In `inout` mode, the endpoint can dynamically reply to the last received client, enabling simple request/response patterns.
-   **Cross-platform**: Built with standard C++ and CMake, making it portable across different operating systems.

## Requirements

- C++20 compatible compiler (e.g., GCC, Clang, MSVC)
- CMake (version 3.16 or later)
- GoogleTest (for running tests, automatically fetched by CMake)

## How to Build

You can build the project using standard CMake commands.

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/your-username/hakoniwa-pdu-endpoint.git
    cd hakoniwa-pdu-endpoint
    ```

2.  **Generate build files**:
    Create a `build` directory and run CMake from the project's root directory.
    ```bash
    cmake -S . -B build
    ```

3.  **Compile the project**:
    ```bash
    cmake --build build
    ```
    This will compile the library and the test executable. The library `libhakoniwa_pdu_endpoint.a` will be located in the `build/src` directory.

## How to Run Tests

The project includes a test suite built with GoogleTest to verify the endpoint's functionality.

To run the tests, execute `ctest` from the project root after a successful build:

```bash
ctest --test-dir build/test --output-on-failure
```

You should see output indicating that all tests have passed.

## Endpoint Configuration

This library supports two main types of endpoints: **Raw Endpoints** for direct network I/O and **Smart Endpoints** for in-memory PDU processing and storage. Configuration is done via a JSON file, and the available schemas can be found in `config/schema/`:

- `udp_endpoint_schema.json`
- `tcp_endpoint_schema.json`
- `buffer_smart_endpoint_schema.json`

### Raw Endpoints (Network I/O)

Raw Endpoints are used for low-level network communication over UDP or TCP. They are responsible for sending and receiving raw byte streams.

#### UDP Endpoint Example
Here is an example of a bidirectional (`inout`) UDP endpoint that listens on port `7000` and can dynamically reply to any client that sends it a message.

**`udp_config.json`**
```json
{
  "protocol": "udp",
  "name": "command_control",
  "direction": "inout",
  "local": {
    "address": "127.0.0.1",
    "port": 7000
  },
  "options": {
    "buffer_size": 8192,
    "timeout_ms": 1000,
    "blocking": true
  }
}
```
For more details, refer to `config/sample/udp_endpoint.json`.

#### TCP Endpoint Example
TCP configurations require a `role` (`server` or `client`). Here is an example of a TCP client that connects to a server.

**`tcp_config.json`**
```json
{
  "protocol": "tcp",
  "name": "command_client",
  "direction": "inout",
  "role": "client",
  "remote": {
    "address": "127.0.0.1",
    "port": 7777
  },
  "options": {
    "connect_timeout_ms": 2000,
    "read_timeout_ms": 1000,
    "write_timeout_ms": 1000,
    "no_delay": true
  }
}
```
For more details, refer to `config/sample/tcp_endpoint.json`.

### Smart Endpoints (In-Memory Processing)

Smart Endpoints work with structured PDU data rather than raw bytes. They provide higher-level functionalities like caching, queueing, and filtering, and can be chained together to form processing pipelines.

#### Buffer Smart Endpoint
The `BufferSmartEndpoint` is an in-memory storage endpoint that can operate in two modes.

**1. "latest" Mode (State Cache)**
This mode stores only the most recent PDU for each channel, making it ideal for caching state information where only the current value matters.

**`latest_mode_config.json`**
```json
{
  "type": "buffer",
  "name": "vehicle_state_buffer",
  "store": {
    "mode": "latest"
  }
}
```

**2. "queue" Mode (Event Queue)**
This mode maintains a FIFO (First-In, First-Out) queue of PDUs up to a specified `depth`. It is suitable for handling events where every PDU must be processed in order.

**`queue_mode_config.json`**
```json
{
  "type": "buffer",
  "name": "collision_event_queue",
  "store": {
    "mode": "queue",
    "depth": 8
  }
}
```

For more examples, please refer to `config/sample/buffer_smart_endpoint.json`.
