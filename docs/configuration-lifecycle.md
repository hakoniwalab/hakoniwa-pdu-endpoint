# Configuration Lifecycle

This document defines the configuration lifecycle for the in-memory configuration API proposed by Issue #35.

The purpose of this specification is to make endpoint configuration easier to construct programmatically while preserving the existing JSON configuration contract and keeping the effective runtime configuration auditable.

## Compatibility principle

The existing JSON configuration format is the compatibility boundary.

- Existing endpoint, communication, cache/buffer, and endpoint-container JSON files MUST continue to load without modification.
- This proposal MUST NOT require a JSON schema migration.
- Existing file-based APIs MUST remain supported.
- The in-memory API is an additional configuration path, not a replacement for file-based configuration.
- Loading an existing JSON configuration and constructing an equivalent in-memory configuration MUST converge on the same effective configuration model.

## Configuration lifecycle

Two entry paths are supported.

Existing configuration:

```text
existing JSON files
        |
        v
      load
        |
        v
in-memory configuration
        |
        v
      execute
```

User-friendly configuration:

```text
default configuration
        |
        v
modify in memory
        |
        v
effective configuration
        |
        +----> persist JSON (audit artifact)
        |
        v
      execute
```

The effective in-memory configuration is the canonical runtime representation regardless of how it was created.

## Default configuration

The API SHOULD be able to construct a usable default configuration in memory without requiring the caller to prepare configuration files first.

Defaults MUST be explicitly defined for the configuration layers needed by an endpoint:

- endpoint
- communication (`comm`)
- cache/buffer
- endpoint container

Protocol-specific defaults MUST be documented rather than inferred implicitly at runtime. Existing generator defaults and existing sample configurations should be used as the starting point when defining these values.

A default configuration is not a separate configuration format. It MUST be representable using the existing JSON format.

## Modification

Callers MAY modify the in-memory configuration before execution.

Modification MUST operate on the same model produced by loading existing JSON files. This avoids maintaining separate semantics for generated and file-loaded configurations.

The intended lifecycle is therefore:

```text
default -> modify -> effective configuration
```

rather than applying hidden runtime overrides after configuration has been finalized.

## Persistence and auditability

The effective configuration MUST be serializable to the existing JSON configuration format.

Persistence is part of the intended lifecycle, not merely a debugging convenience. The generated files provide an audit artifact showing the configuration that was finalized for execution.

The caller SHOULD be able to explicitly choose the output directory.

When no output directory is specified, the proposed default directory is:

```text
config/runtime/
```

This follows the existing distinction between repository-managed sample configuration and runtime-generated artifacts:

```text
config/sample/    # examples and repository-managed static configuration
config/runtime/   # generated/effective runtime configuration
```

The exact filenames SHOULD remain deterministic and compatible with the current generator convention where applicable:

```text
endpoint_<name>.json
comm_<name>.json
endpoint_container_<name>.json
```

Cache/buffer persistence and naming MUST be specified when the canonical default cache/buffer model is finalized.

A user-specified output path takes precedence over the default output directory.

## EndpointContainer responsibility

`EndpointContainer` SHOULD consume a finalized configuration. It SHOULD NOT own default generation, configuration editing, or persistence policy.

Conceptually:

```text
configuration construction / load
            |
            v
configuration modification
            |
            v
configuration persistence
            |
            v
    effective configuration
            |
            v
      EndpointContainer
```

Existing APIs that accept configuration file paths remain valid and may internally load those files into the same effective configuration representation.

## Round-trip invariant

Configuration serialization MUST preserve semantic configuration equality.

For an existing configuration:

```text
load(existing JSON)
    -> save(JSON)
    -> load(saved JSON)
```

The first and final in-memory configurations MUST be semantically equivalent.

For generated configuration:

```text
default()
    -> modify()
    -> save(JSON)
    -> load(saved JSON)
```

The modified configuration and the reloaded configuration MUST be semantically equivalent.

JSON formatting details such as object key order, indentation, and trailing newlines are not part of this invariant.

## Implementation sequence

Implementation should proceed in three independent stages.

1. **Specification** — agree on this lifecycle, compatibility boundary, default model, and persistence behavior.
2. **Characterization and regression tests** — establish semantic round-trip tests for existing JSON configurations before changing configuration internals.
3. **Implementation** — introduce the in-memory configuration API, default construction, modification, serialization, and `EndpointContainer` integration.

The implementation stage MUST NOT begin by changing the existing JSON specification. If a future JSON format change is required, it should be proposed and reviewed separately.

## Test requirements for the next stage

The regression-test PR should cover representative existing configurations for endpoint, comm, cache/buffer, and endpoint-container configuration and establish at least these invariants:

- existing JSON loads successfully without modification;
- load -> save -> load preserves semantic equality;
- default -> save -> load preserves semantic equality;
- default -> modify -> save -> load preserves semantic equality;
- existing file-based endpoint/container initialization remains valid.

These tests form the compatibility guardrail for the subsequent implementation of Issue #35.
