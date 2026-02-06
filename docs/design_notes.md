# Design Notes

## Summary

This library deliberately prioritizes explicit simulation semantics over minimal configuration. It is optimized for distributed, multi-asset simulations where causality, delivery guarantees, and data lifetime must be auditable.

## Core Trade-off

- **Gain:** Explicit cache/comm/pdu_def separation makes semantics visible and extensible.
- **Cost:** Initial configuration is more verbose, and some integration points are explicit by design.

## Explicitness as a Feature

- **Cache** defines data lifetime and overwrite semantics.
- **Comm** defines delivery guarantees and failure modes.
- **PDU Definition** defines the meaning of bytes (name → channel_id/size).

Implicit behavior is rejected because it hides simulation semantics and makes results harder to verify.

## Integration Control (SHM Poll)

In SHM poll mode, users must call `process_recv_events()` explicitly. This is intentional: it avoids hidden background threads and allows integration with external event loops (e.g., game or simulation engines) that require ownership of scheduling.

## Scope

This architecture is a strong fit for complex distributed simulations. For simple point-to-point messaging where defaults are acceptable, the explicit configuration may feel heavy.

## Diagrams

See `docs/diagrams/README.md` for visual summaries.
