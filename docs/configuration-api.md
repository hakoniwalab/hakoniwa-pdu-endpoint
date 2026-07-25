# Default Configuration API Specification

This document specifies the Default Configuration API for `hakoniwa-pdu-endpoint`, as proposed in [Issue #35](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/issues/35) and building upon the design principles outlined in `docs/configuration-lifecycle.md`.

## 1. Overview

The primary goal of this API is to support **file-less runtime configuration**: endpoint configuration can be constructed in memory and consumed directly by the endpoint runtime without requiring a configuration file as an input.

A caller selects a supported communication provider, starts from its canonical default configuration, and optionally customizes only the required fields using a partial JSON object. The resulting effective configuration is then used directly for endpoint initialization.

The effective configuration may also be persisted as JSON for **auditability, reproducibility, and debugging**. Persistence is optional and is not required for runtime execution.

This API does not introduce a new configuration format. It provides a programmatic path for constructing configurations that conform to the existing JSON configuration structure and validation model.

## 2. Design Principles

- **File-less Runtime Configuration**: Runtime execution must not require an intermediate configuration file. The effective configuration is kept in memory and consumed directly by endpoint initialization.
- **Communication-Independent API**: The top-level API for creating configurations is independent of the underlying communication (`comm`) protocol. This ensures that future additions of `comm` providers will not require changes to the client-side API bindings (C++, C, Python, Godot, etc.).
- **JSON-based Options**: Configuration options are passed as a simple JSON text string. This avoids the need for protocol-specific data structures in the C-ABI and other language bindings, preventing breaking changes when options are added or modified.
- **Partial JSON Overlay**: Customizations are applied by overlaying a partial JSON object onto the default configuration. This reuses the existing, well-understood JSON structure and avoids introducing a new language for options.
- **Explicit Failure**: The API is designed to fail explicitly for invalid inputs, such as malformed JSON or unsupported option keys. It will not silently ignore errors or fall back to default values for unrecognized options (e.g., misspelled keys).
- **Optional Persistence**: Saving the effective configuration is a secondary operation for audit and reproduction purposes; it is not part of the mandatory runtime path.

## 3. API Contract

### Conceptual API

The configuration creation API can be represented by the following conceptual function:

```cpp
// Conceptual C++
EffectiveConfiguration create_default(
    const std::string& provider_name,
    const std::string& options_json = "{}"
);
```

- `provider_name`: A string identifying the configuration provider, typically corresponding to a `comm` type (e.g., `"tcp"`, `"udp"`).
- `options_json`: An optional JSON string representing the partial configuration to overlay on the default.

The returned `EffectiveConfiguration` is an in-memory runtime object. The endpoint initialization path must be able to consume this object directly, without first serializing it to a file.

Conceptually, the runtime path is:

```text
default configuration
        +
partial JSON options
        |
        v
effective configuration (in memory)
        |
        v
endpoint initialization / execution
```

Persistence is an optional side path:

```text
effective configuration
        |
        +--> save as JSON (audit / reproduction / debugging)
```

### C-ABI Example

The C-ABI will be designed to be stable and protocol-agnostic. Specific endpoint-initialization signatures will be finalized during implementation, but they must preserve the following contract:

```c
// Conceptual C-ABI
// The returned handle represents the in-memory configuration object.
hako_pdu_config_t* hako_pdu_config_create_default(
    const char *provider_name,
    const char *options_json,
    hako_pdu_error_t* err
);

// The endpoint runtime consumes the in-memory configuration directly.
// The exact endpoint initialization API is implementation-specific and
// will be finalized during the implementation phase.

// Optionally persists the effective configuration for auditing,
// reproducibility, or debugging.
hako_pdu_error_t hako_pdu_config_save(
    hako_pdu_config_t* config,
    const char* path
);

// Frees the in-memory configuration object.
void hako_pdu_config_destroy(hako_pdu_config_t* config);
```

The important compatibility contract is that language bindings operate on a stable configuration handle plus JSON text, rather than protocol-specific option structures.

## 4. Configuration Providers

The responsibility for defining default values and supported override keys belongs to the component that owns the corresponding configuration semantics.

Conceptually, a `comm` provider defines:

- The canonical default configuration.
- The set of overridable option keys.
- The logic for applying the partial JSON overlay.

The common configuration layer remains independent of protocol-specific defaults.

If a `comm` implementation does not support default configuration generation, attempting to create a default configuration for it results in an explicit unsupported-provider error. Such configurations must continue to be supplied through the existing manual configuration path.

### Initial implementation note

The provider abstraction is a **responsibility boundary**, not a requirement to introduce a general provider framework in the first implementation.

The initial implementation may use a simple dispatch for the supported provider, for example:

```cpp
if (provider_name == "tcp") {
    // create TCP defaults and apply supported overrides
} else {
    return UNSUPPORTED_PROVIDER;
}
```

A more extensible registration or provider mechanism may be introduced later when multiple implementations justify it.

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

The supported provider merges this onto its canonical default configuration.

Only explicitly supported keys may be overridden. Unknown or unsupported keys result in an error rather than being ignored.

## 6. Validation Strategy

Validation is performed in stages to avoid duplicating logic.

### Phase 1: Initial Checks (This API's Scope)

The default configuration creation path performs the following minimal checks:

1. **JSON Parsability**: Verify that `options_json` is a valid JSON string.
2. **JSON Type**: Verify that the parsed JSON is an object.
3. **Supported Keys**: Verify that all top-level and nested keys in the `options_json` are known and supported by the selected provider. An unknown key results in an error.

This phase intentionally implements only the minimum validation required to safely construct the effective configuration.

### Phase 2: Full Schema Validation

Detailed validation of values (e.g., type, enum, range, semantic rules) remains the responsibility of the existing runtime configuration validation layer, which already uses JSON Schema.

The effective configuration generated by this API must pass through the same authoritative validation rules before runtime execution as file-based configuration.

This separates concerns:

- **This API**: Constructs an effective configuration in memory from defaults plus supported overrides.
- **Existing validation layer**: Validates the complete effective configuration before use.

This avoids maintaining two independent validation rule sets.

## 7. Runtime Consumption

The effective configuration is primarily a runtime object, not a file-generation artifact.

The endpoint initialization flow must accept the effective in-memory configuration directly. Serializing the configuration and reading it back from disk must not be required for normal execution.

The exact integration point with the existing endpoint initialization API will be finalized during implementation, but the following behavior is required:

1. Create the default configuration for a supported provider.
2. Apply the caller's supported JSON overrides.
3. Validate the resulting effective configuration using the existing validation rules.
4. Initialize the endpoint directly from the resulting in-memory configuration.

## 8. Persistence and Auditability

The final effective configuration can optionally be saved as JSON. This is a secondary capability intended for operational visibility rather than runtime dependency.

Typical uses include:

- Recording the exact configuration used for a run.
- Reproducing a previously executed setup.
- Comparing generated configuration during debugging.
- Retaining an auditable runtime artifact.

The API will provide a save operation such as `hako_pdu_config_save`.

The caller may specify an output path or directory. If the implementation supports a default output location, it should remain consistent with the runtime configuration conventions defined in `configuration-lifecycle.md` (for example, `config/runtime/`).

Failure to persist an optional audit copy is distinct from the ability to construct and use the configuration in memory; normal runtime execution does not require a saved file unless explicitly requested by the caller's workflow.

## 9. Initial Implementation Scope

The first implementation will focus on the minimum useful end-to-end path:

1. **Common Configuration API**: Language-agnostic C-ABI and C++ representation for an in-memory effective configuration.
2. **TCP Defaults**: Canonical default configuration for TCP as the representative first provider.
3. **Minimal JSON Overlay**: Support only the initial documented set of TCP override keys.
4. **Explicit Unsupported-input Errors**: Reject malformed JSON, unknown keys, and unsupported providers.
5. **Runtime Consumption**: Allow endpoint initialization to consume the effective configuration directly from memory.
6. **Optional Persistence**: Allow the effective configuration to be saved as JSON for auditing, reproduction, and debugging.

Default support for other `comm` types (UDP, MQTT, Zenoh, etc.) will be added incrementally in separate pull requests.

The initial implementation should prefer simple, explicit code over introducing a generalized provider framework before multiple providers require it.

## 10. Testing Strategy

Tests will target the **API contract**, not the internal implementation details of JSON serialization.

The core behavior to verify is:

- Default configuration creation succeeds for the supported provider.
- Supported partial overrides are applied correctly.
- Malformed JSON and unsupported keys fail explicitly.
- Unsupported providers fail explicitly.
- The resulting configuration can be consumed directly by endpoint initialization without a configuration file.
- Saving and reloading the audit artifact reproduces the same effective configuration semantics.

This approach keeps the tests durable and allows bindings in other languages (Python, C#, Godot, etc.) to be validated against the same core behavior.

## 11. Out of Scope

The following items are explicitly **not** part of the initial implementation:

- Default providers for all `comm` types.
- A generalized provider registration framework.
- A new configuration file format or schema.
- A new, generic configuration validator.
- Duplicating JSON Schema validation rules inside the default configuration creator.
- Broad support for every possible TCP configuration field in the first version.
- The implementation of C-bindings for cffi (Python), C#, or Godot.
