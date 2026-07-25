# Cache Configuration API Specification

This document defines the programmatic configuration model for endpoint caches.

It complements `docs/configuration-api.md`, which defines the broader file-less endpoint configuration flow and the extensible configuration strategy used for communication (`comm`) implementations.

## 1. Scope

An endpoint is composed from two configuration concerns with different complexity profiles:

- **Cache**: local storage semantics owned by the endpoint runtime.
- **Comm**: transport-specific behavior owned by each communication implementation.

These concerns should not be forced into the same configuration abstraction.

The current cache model is intentionally small and stable: a cache is a buffer using either `latest` or `queue` storage semantics, with `depth` required only for queue mode.

Therefore, cache configuration should use a **small typed API**, rather than the provider + JSON-overlay mechanism used for extensible comm configuration.

## 2. Design Principles

- **Typed and explicit**: Cache configuration uses a small typed structure or equivalent C-ABI fields.
- **No provider abstraction**: Cache configuration does not introduce a provider registry or provider-name dispatch.
- **No JSON text required at the API boundary**: Callers should not need to construct JSON merely to select `latest`, `queue`, or queue depth.
- **Same effective configuration model**: Programmatic cache configuration and file-loaded cache configuration converge on the same runtime representation and validation semantics.
- **Backward compatible**: Existing cache JSON remains supported as an external configuration format.
- **Persistable**: The effective cache configuration can be serialized to the existing JSON form as part of the audit/reproduction path defined in `docs/configuration-api.md`.

## 3. Current Cache Model

The existing cache schema defines:

- `type`: fixed to `"buffer"`.
- `name`: cache endpoint name.
- `store.mode`: `"latest"` or `"queue"`.
- `store.depth`: required for queue mode, valid in the existing schema range.

This limited surface does not justify a generic provider mechanism.

Conceptually:

```text
CacheConfig
  name
  mode = latest | queue
  depth = N          # queue only
```

## 4. Conceptual C++ API

The exact implementation types may differ, but the API should remain equivalent to a small typed model:

```cpp
enum class CacheMode {
    Latest,
    Queue,
};

struct CacheConfig {
    std::string name;
    CacheMode mode = CacheMode::Latest;
    std::size_t depth = 1;
};
```

Convenience constructors/factories may be provided:

```cpp
CacheConfig make_latest_cache(const std::string& name);
CacheConfig make_queue_cache(const std::string& name, std::size_t depth);
```

For `Latest`, `depth` is not semantically used. For `Queue`, `depth` must satisfy the same constraints as the existing cache JSON schema.

## 5. Conceptual C-ABI

Language bindings should receive an equally small and stable contract. For example:

```c
typedef enum {
    HAKO_PDU_CACHE_LATEST = 0,
    HAKO_PDU_CACHE_QUEUE = 1,
} hako_pdu_cache_mode_t;

typedef struct {
    const char *name;
    hako_pdu_cache_mode_t mode;
    size_t depth;
} hako_pdu_cache_config_t;
```

The final C-ABI may use opaque handles if that better matches the configuration API implementation, but cache-specific JSON text should not be required simply to express the current cache semantics.

## 6. Endpoint Composition

The broader endpoint configuration API should compose cache and comm configuration rather than treating them as the same kind of provider.

Conceptually:

```text
EndpointConfig
  +-- CacheConfig       # typed, simple
  +-- CommConfig        # provider + JSON options, extensible
  +-- endpoint options  # e.g. recv_cache_write, PDU definition metadata
```

The intended runtime flow is:

```text
typed CacheConfig ----+
                      |
Comm effective config +--> Endpoint effective config --> validation --> runtime
                      |
endpoint options -----+
```

No intermediate cache configuration file is required for runtime execution.

## 7. Validation

Programmatic cache configuration should apply only the validation needed to construct a valid effective cache configuration and should reuse the existing authoritative constraints.

At minimum:

- `name` must be non-empty and within the existing schema limit.
- `mode` must be a supported `CacheMode` value.
- queue `depth` must be within the existing schema range.

The API must fail explicitly for invalid values rather than silently normalizing them.

## 8. Persistence and Auditability

Typed cache configuration must remain serializable to the existing cache JSON representation.

For example:

```cpp
make_queue_cache("events", 16)
```

is semantically equivalent to:

```json
{
  "type": "buffer",
  "name": "events",
  "store": {
    "mode": "queue",
    "depth": 16
  }
}
```

This serialization is optional for execution and exists for auditability, reproducibility, debugging, and compatibility with existing file-based workflows.

## 9. Initial Implementation Scope

The first implementation should stay deliberately small:

1. Add the typed cache configuration representation.
2. Support `latest` and `queue` modes.
3. Validate queue depth using the existing cache constraints.
4. Allow endpoint configuration/initialization to consume the typed cache configuration directly.
5. Allow the effective cache configuration to be serialized into the existing JSON form for audit/persistence.

No generalized cache provider mechanism is needed.

## 10. Out of Scope

The following are intentionally outside this initial design:

- Cache provider registries or plug-in discovery.
- Cache configuration via provider-name strings.
- Requiring JSON overlay text for the current cache options.
- Adding new cache algorithms or storage modes.
- Changing the existing cache JSON schema.
- Removing file-based cache configuration support.

If cache semantics become substantially more extensible in the future, this decision can be revisited based on actual complexity rather than anticipated complexity.
