# FAQ

**Q: Why does this project require so many configuration files?**
A: Each file encodes a separate semantic decision: cache controls data lifetime and overwrite semantics, comm controls delivery guarantees and failure modes, and pdu_def fixes the meaning of bytes. Keeping these decisions explicit avoids ambiguity in distributed simulation.

**Q: Why not infer defaults automatically?**
A: Inference hides causality and delivery assumptions. That can be acceptable in app-layer messaging, but it is risky in simulation where timing, loss, and overwrite semantics affect correctness.

**Q: Why are the examples so small?**
A: They are executable reference configurations, not tutorials. Small examples make the protocol and cache semantics visible without extra scaffolding.

**Q: Is this just a messaging library?**
A: No. An Endpoint defines the causality boundary between components and fixes semantics such as data lifetime and delivery behavior, which a generic messaging API does not enforce.

**Q: If configuration is explicit, why provide a generator?**
A: The generator removes boilerplate while keeping semantics explicit. It never guesses cache or delivery behavior; it only fills protocol basics and prints what still requires a deliberate choice.

**Q: Why is configuration split across multiple JSON files instead of a single file or code-based API?**
A: The split encodes separable semantic decisions (cache/comm/pdu_def), enables runtime switching without recompilation, and improves auditability via validators and generators. Single-file or code-based approaches can be simpler locally, but they trade away runtime flexibility or push coupling elsewhere. This choice is intentional for distributed simulation semantics.

**Q: Isn’t it risky that `pdu_def_path` toggles the API level?**
A: The behavior is explicit by configuration, and `validate_json --check-paths` catches missing paths early. SHM requires PDU definitions by design. The approach favors runtime declarative composition over compile-time enforcement; use the generator + validator in CI to keep configs correct.

**Q: Can I use this without Hakoniwa?**
A: Yes. TCP/UDP/WebSocket modes do not depend on Hakoniwa. SHM and Hakoniwa time sources require the Hakoniwa core libraries.

**Q: When should I NOT use this component?**
A: If you want a lightweight messaging API with implicit defaults and minimal configuration, or if you do not need explicit simulation semantics, this is likely heavier than necessary.

**Q: Why do I have to call `process_recv_events()` for SHM poll?**
A: SHM poll mode is designed to integrate with external event loops (e.g., game/simulation loops) that control threading. The explicit call avoids hidden background threads and makes scheduling responsibility visible.
