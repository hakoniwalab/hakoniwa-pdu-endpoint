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

The API SHOULD make common endpoint configurations easy to construct in memory without requiring the caller to prepare configuration files first.

A default configuration is not a separate configuration format. Every generated default MUST be representable using the existing JSON format.

### Default ownership

Defaults are owned by the layer or component that owns the corresponding configuration semantics.

The common endpoint layer MAY define defaults only for configuration that belongs to the common endpoint model, such as endpoint metadata, common cache/buffer structure, endpoint-container structure, persistence location, and other fields whose semantics are independent of a specific communication implementation.

Protocol- or implementation-specific defaults MUST be owned by the corresponding communication (`comm`) implementation. The common layer MUST NOT invent values for an extensible communication implementation merely to make every implementation default-constructible.

Conceptually:

```text
common configuration layer
├── endpoint defaults
├── common cache/buffer defaults
├── endpoint-container defaults
└── comm selection
      |
      +--> comm implementation
             └── optional default provider
```

This ownership rule also applies to configuration owned by external adapters or integration components. The endpoint layer may reference such configuration where the existing JSON contract requires it, but it does not own or redefine that component's defaults.

### Defaults are optional capabilities

A communication implementation is not required to provide a default configuration.

For a communication implementation with a default provider:

```text
request default for comm type
        |
        v
comm-owned default provider
        |
        v
existing-JSON-compatible comm configuration
```

For a communication implementation without a default provider, default construction MUST fail explicitly and require the caller to provide configuration. It MUST NOT silently guess implementation-specific values.

Conceptually:

```text
default_for("supported-comm")
    -> default configuration

default_for("explicit-config-required-comm")
    -> explicit configuration required
```

The exact API and error type are implementation details to be decided after the behavior is covered by tests.

### Initial implementation scope

Issue #35 does not require every communication implementation to gain a default provider in the first implementation.

The first implementation SHOULD establish the reusable pattern with:

1. the common in-memory configuration layer;
2. common persistence and round-trip behavior;
3. common endpoint/cache/container defaults that can be defined without protocol-specific assumptions; and
4. one representative communication implementation with a documented default provider.

TCP is the preferred representative communication implementation because existing generator presets already provide concrete server/client configurations that can be evaluated as the starting point for canonical defaults.

Other communication implementations SHOULD be added incrementally in separate changes by following the same pattern:

```text
default specification
        -> default provider
        -> round-trip/regression tests
```

Until a communication implementation has an approved default provider, callers MUST continue to provide its configuration explicitly using the existing configuration mechanisms.

This keeps the first implementation small while making subsequent default-provider additions mechanical and independently reviewable.

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

Cache/buffer persistence and naming MUST be specified when the canonical common cache/buffer default is finalized.

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

1. **Specification** — agree on this lifecycle, compatibility boundary, default ownership/provider model, initial representative scope, and persistence behavior.
2. **Characterization and regression tests** — establish semantic round-trip tests for existing JSON configurations before changing configuration internals.
3. **Implementation** — introduce the common in-memory configuration API, common defaults, representative comm default provider, modification, serialization, and `EndpointContainer` integration.

The implementation stage MUST NOT begin by changing the existing JSON specification. If a future JSON format change is required, it should be proposed and reviewed separately.

Additional communication default providers are follow-up extensions and are not required to complete the initial implementation pattern.

## Test requirements for the next stage

The regression-test PR should cover representative existing configurations for endpoint, comm, cache/buffer, and endpoint-container configuration and establish at least these invariants:

- existing JSON loads successfully without modification;
- load -> save -> load preserves semantic equality;
- common default -> save -> load preserves semantic equality;
- common default -> modify -> save -> load preserves semantic equality;
- a representative supported comm default -> save -> load preserves semantic equality;
- requesting a default for a comm without a default provider fails explicitly rather than inventing configuration;
- existing file-based endpoint/container initialization remains valid.

These tests form the compatibility guardrail for the subsequent implementation of Issue #35 and the template for adding default providers to other communication implementations.