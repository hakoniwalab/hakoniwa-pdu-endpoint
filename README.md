# hakoniwa-pdu-endpoint

`hakoniwa-pdu-endpoint` is a C++ library that provides a file-based configuration for UDP/TCP communication endpoints, designed for Hakoniwa PDU (Protocol Data Unit) communication. It allows for flexible setup of sockets by defining communication parameters in a simple JSON file.

## Features

- **JSON-based Configuration**: Easily configure UDP/TCP endpoints, including IP addresses, ports, and various socket options.
- **Multiple Communication Directions**: Supports `in` (receive-only), `out` (send-only), and `inout` (bidirectional) communication modes.
- **Dynamic Replies**: In `inout` mode, the endpoint can dynamically reply to the last received client, enabling simple request/response patterns.
- **Cross-platform**: Built with standard C++ and CMake, making it portable across different operating systems.

## Requirements

- C++17 compatible compiler (e.g., GCC, Clang, MSVC)
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

## Configuration Example

Configuration is done via a JSON file. The schemas can be found in:

- UDP: `config/schema/udp_endpoint_schema.json`
- TCP: `config/schema/tcp_endpoint_schema.json`

TCP configurations require a `role` that indicates connection behavior:

- `client`: actively connects to `remote`
- `server`: listens on `local` and accepts connections

Here is an example of a bidirectional (`inout`) endpoint that listens on port `7000` and can dynamically reply to any client that sends it a message:

**`my_config.json`**
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

For more examples, please refer to `config/sample/udp_endpoint.json`.

## TCP Configuration Example

Here is an example of a bidirectional TCP client that connects to a server:

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

For more examples, please refer to `config/sample/tcp_endpoint.json`.
