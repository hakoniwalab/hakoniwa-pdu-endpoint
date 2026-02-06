# FAQ

**Q: Why does this project require so many configuration files?**
A: Each file encodes a separate semantic decision: cache controls data lifetime and overwrite semantics, comm controls delivery guarantees and failure modes, and pdu_def fixes the meaning of bytes. Keeping these decisions explicit avoids ambiguity in distributed simulation.

**Q: Why not infer defaults automatically?**
A: Inference hides causality and delivery assumptions. That can be acceptable in app-layer messaging, but it is risky in simulation where timing, loss, and overwrite semantics affect correctness.

**Q: Why are the examples so small?**
A: They are executable reference configurations, not tutorials. Small examples make the protocol and cache semantics visible without extra scaffolding.

**Q: Is this just a messaging library?**
A: No. An Endpoint defines the causality boundary between components and fixes semantics such as data lifetime and delivery behavior, which a generic messaging API does not enforce.

**Q: Can I use this without Hakoniwa?**
A: Yes. TCP/UDP/WebSocket modes do not depend on Hakoniwa. SHM and Hakoniwa time sources require the Hakoniwa core libraries.

**Q: When should I NOT use this component?**
A: If you want a lightweight messaging API with implicit defaults and minimal configuration, or if you do not need explicit simulation semantics, this is likely heavier than necessary.
