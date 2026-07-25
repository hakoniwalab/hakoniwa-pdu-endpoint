# Cache configuration persistence

`CacheConfig` is the runtime-facing typed representation. JSON persistence is an optional boundary for auditability, reproducibility, and debugging.

The persistence API supports:

- `cache_config_to_json()` / `cache_config_from_json()` for in-memory conversion
- `save_cache_config()` / `load_cache_config()` for file persistence
- the existing cache JSON shape (`type: buffer`, `mode: latest|queue`, optional queue `depth`)

Persistence is not required for runtime execution. This step intentionally does not change `PduCache::open(config_path)` or Endpoint initialization; direct consumption of `CacheConfig` is a separate implementation step.
