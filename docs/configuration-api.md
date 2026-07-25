# Default Configuration API Specification

This document specifies the Default Configuration API for `hakoniwa-pdu-endpoint`, as proposed in [Issue #35](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/issues/35) and building upon the design principles outlined in `docs/configuration-lifecycle.md`.

## 1. Overview

The primary goal of this API is to simplify the programmatic creation of endpoint configurations. It allows users to generate a default configuration for a specific communication protocol, optionally customize it with partial settings, and persist the final, effective configuration as a JSON file for auditing and debugging.

This API does not introduce a new configuration format but rather provides a more convenient way to construct configurations that conform to the existing JSON schema.

## 2. Design Principles

- **Communication-Independent API**: The top-level API for creating configurations is independent of the underlying communication (`comm`) protocol. This ensures that future additions of `comm` providers will not require changes to the client-side API bindings (C++, C, Python, Godot, etc.).
- **JSON-based Options**: Configuration options are passed as a simple JSON text string. This avoids the need for protocol-specific data structures in the C-ABI and other language bindings, preventing breaking changes when options are added or modified.
- **Partial JSON Overlay**: Customizations are applied by overlaying a partial JSON object onto the default configuration. This reuses the existing, well-understood JSON structure and avoids introducing a new language for options.
- **Explicit Failure**: The API is designed to fail explicitly for invalid inputs, such as malformed JSON or unsupported option keys. It will not silently ignore errors or fall back to default values for unrecognized options (e.g., misspelled keys).

## 3. API Contract

### Conceptual API

The core of the API can be represented by a single conceptual function:

```cpp
// Conceptual C++
EffectiveConfiguration create_default(
    const std::string& provider_name,
    const std::string& options_json = "{}"
);
```

- `provider_name`: A string identifying the configuration provider, typically corresponding to a `comm` type (e.g., `"tcp"`, `"udp"`).
- `options_json`: An optional JSON string representing the partial configuration to overlay on the default.

### C-ABI Example

The C-ABI will be designed to be stable and protocol-agnostic. A specific function signature will be finalized during implementation, but it will follow this principle:

```c
// Conceptual C-ABI
// The returned handle represents the in-memory configuration object.
hako_pdu_config_t* hako_pdu_config_create_default(
    const char *provider_name,
    const char *options_json,
    hako_pdu_error_t* err
);

// Persists the configuration to a file.
hako_pdu_error_t hako_pdu_config_save(
    hako_pdu_config_t* config,
    const char* path
);

// Frees the in-memory configuration object.
void hako_pdu_config_destroy(hako_pdu_config_t* config);
```

## 4. Configuration Providers

The responsibility for providing default configurations lies with the components that own the configuration semantics.

- **`comm` Providers**: Each communication implementation (e.g., TCP, UDP, Zenoh) is responsible for its own optional "default provider." This provider defines:
    - The canonical default configuration.
    - The set of overridable option keys.
    - The logic for merging (overlaying) user-provided options.
- **Common Layer**: The common configuration layer does not know the specifics of protocol defaults.
- **Unsupported `comm`s**: If a `comm` implementation does not have a default provider, attempting to generate a default configuration for it will result in an explicit error. All configuration for such `comm`s must be provided manually.

## 5. Options Overlay Mechanism

Options are applied as a partial JSON overlay. The structure of the `options_json` must mirror the structure of the full JSON configuration file for the corresponding component.

For example, to create a default TCP server configuration but override the port, the `options_json` would be:

```json
{
  "role": "server",
  "local": {
    "port": 55000
  }
}
```

The provider will merge this onto its default TCP configuration.

## 6. Validation Strategy

Validation is performed in stages to avoid duplicating logic.

### Phase 1: Initial Checks (This API's Scope)

The default provider will perform the following minimal checks:
1.  **JSON Parsability**: Verify that `options_json` is a valid JSON string.
2.  **JSON Type**: Verify that the parsed JSON is an object.
3.  **Supported Keys**: Verify that all top-level and nested keys in the `options_json` are known and supported by the provider. An unknown key will result in an error.

### Phase 2: Full Schema Validation (Future Scope)

Detailed validation of values (e.g., type, enum, range, semantic rules) is **out of scope** for the default provider itself. This responsibility remains with the existing runtime configuration loader, which already uses a JSON Schema validator. The effective configuration generated by this API will be passed to that existing validation layer before execution.

This separates concerns:
- **This API**: Constructs a configuration object.
- **Future Work**: Use the existing JSON Schema validator as a common, authoritative validation layer for all configuration, whether from a file or from this API.

## 7. Persistence

The final, effective configuration can be saved to a set of JSON files. This serves as an auditable artifact.

- The API will provide a function to save the configuration (e.g., `hako_pdu_config_save`).
- The user can specify an output directory.
- If no directory is specified, the files will be saved to the default runtime directory (`config/runtime/`), as defined in `configuration-lifecycle.md`.

## 8. Initial Implementation Scope

The first implementation will focus on establishing the core pattern:
1.  **Common Configuration API**: The language-agnostic C-ABI and C++ implementation.
2.  **Persistence**: The ability to save the effective configuration.
3.  **A Representative `comm` Provider**: A default provider for **TCP** will be implemented as the first example.

Default providers for other `comm` types (UDP, MQTT, Zenoh, etc.) will be added incrementally in separate pull requests.

## 9. Testing Strategy

While tests will be implemented in a subsequent pull request, the strategy is defined here:

- Tests will target the **API contract**, not the internal implementation details of JSON serialization (e.g., `json.load`/`json.dump`).
- The goal is to verify the semantics of the configuration API, ensuring that creating, modifying, and persisting a configuration behaves as expected.
- This approach ensures that the tests are durable and can be used to validate bindings in other languages (Python, C#, Godot) against the same core behavior.

## 10. Out of Scope

The following items are explicitly **not** part of this API specification and its initial implementation:

- A full implementation of the API.
- The addition of unit or integration tests.
- Default providers for all `comm` types.
- A new configuration file format or schema.
- A new, generic configuration validator.
- The integration of the JSON Schema validator into the default creation flow.
- The implementation of C-bindings for cffi (Python), C#, or Godot.
