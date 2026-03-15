# Storage Comm status and next steps (issue.md alignment)

## Current implementation status

As of now, `StorageComm` already supports both persistence modes described in `issue.md`.

- `queue` mode: appends every framed packet in send order to a file backend.
- `latest` mode: keeps one latest packet per `(robot, channel_id)` key and updates index metadata.

The implementation is wired through:

- `protocol: "storage"` in comm config schema.
- file backend configuration (`storage.path`).
- mode selection (`storage.mode = latest | queue`).

## Confirmed behavior in tests

The endpoint tests currently verify the main storage behaviors:

- queue mode persists all sends.
- latest mode keeps only the latest packet per channel key.
- latest mode can be read back by an IN endpoint when file/index are valid.
- latest mode rejects uninitialized index entries on open.

## Gap against issue.md

The remaining gap is mostly around the **metadata design depth** and replay-read semantics for `queue` mode.

`issue.md` asks for a more explicit three-layer storage format and richer metadata fields. The current code already has practical headers and indexing, but does not yet expose all suggested metadata fields (for example broad file-level timestamps/ranges) nor a replay-oriented API for queue-mode reads.

## Queue data specification (current implementation)

Current `queue` mode uses a simple append-only binary stream without a file header.

Each record is encoded as:

1. `packet_size` (4 bytes, little-endian `uint32`)
2. `packet_bytes` (`packet_size` bytes)

In pseudo format:

```text
record := <u32_le packet_size> <byte[packet_size] packet>
file   := record*
```

Notes:

- The file has no global metadata area yet (version/mode/index/range are not embedded for queue mode in the current implementation).
- `packet_bytes` is the encoded raw comm packet (`DataPacket::encode(comm_raw_version)`), so `robot`, `channel_id`, and payload are preserved inside the packet body.
- Writers append records in send order by opening the file with `std::ios::app`; therefore queue mode currently guarantees append order but does not yet provide a `queue`-mode `recv()` cursor API.
- Existing test helpers read queue records using the same `<u32_le size> + packet` framing and validate that all sends are persisted in order.

## Suggested implementation order

1. Extend queue-file header/metadata with explicit counts and optional time range fields.
2. Add `queue` read path (`recv`) with sequential cursor semantics.
3. Add optional filtering hooks (`robot`, `channel_id`) for offline/replay readers.
4. Keep backward compatibility by versioning format and accepting existing v1 files.

## Notes on compact PDU baseline

The compact PDU schema remains the right baseline for new work:

- `new-pdudef.json` maps robot to `pdutypes_id`.
- `paths[]` links to `new-pdutypes.json`.
- actual typed entries include `channel_id`, `pdu_size`, `name`, and `type`.

Storage metadata should continue recording actual payload length per record, even when `pdu_size` exists in compact definitions.
