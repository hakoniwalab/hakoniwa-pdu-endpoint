# Storage Comm

`StorageComm` is a file-backed communication backend for persistence, replay, and state snapshot use cases.

It is configured with:

- `protocol: "storage"`
- `storage.backend: "file"`
- `storage.mode: "latest" | "queue"`

This document describes the intended semantics and the currently implemented file formats.

## Design Scope

`StorageComm` is not a generic object store.
It persists encoded PDU packets produced by the existing raw packet layer.

The design goals are:

- preserve packet-level information for later analysis
- support replay-oriented reads for `queue`
- support fixed-slot state snapshots for `latest`
- keep semantics consistent with `PduDefinition`

## API Model

Two receive styles exist because `latest` and `queue` have different semantics.

- `recv(const PduResolvedKey&, ...)`
  - primary API for `latest`
  - compatibility path for key-filtered reads
- `recv_next(PduRecord&)`
  - primary API for `queue`
  - returns the next record in global log order

`PduRecord`:

```cpp
struct PduRecord {
    PduResolvedKey key;
    uint64_t timestamp_ns;
    std::vector<std::byte> payload;
};
```

## Debug Tool

A CLI inspector is available for storage files:

```bash
build/tools/hako_pdu_storage_debug <storage-file> [--limit N] [--json] [--verbose]
```

It reads `StorageHeader`, detects `latest` or `queue`, and prints:

- file summary
- key information
- packet offsets and sizes
- `latest` storage timestamps
- decoded packet metadata such as request type and packet timestamps

With `--json`, it emits one JSON document containing:

- file-level metadata
- `records[]`
- common fields such as `key`, `packet_offset`, `packet_size`
- `latest` storage timestamps
- decoded packet metadata when available

## Unified Storage Structures

The intended direction is to keep the storage metadata vocabulary unified across `latest` and `queue`.

At the conceptual level:

- `StorageHeader`
  - file-level metadata
- `StorageEntry`
  - one metadata entry describing one stored packet

The difference between `latest` and `queue` is not the entry shape itself, but:

- how entries are arranged
- how they are updated
- how they are consumed

## Why `next_offset` Is Not Required

For `queue`, a `next_offset` field is not strictly required if each entry already has:

- `packet_offset`
- `packet_size`

Because the next position can be computed as:

```text
next_offset = packet_offset + packet_size
```

In the current implementation, `queue` is even simpler than that:

- records are physically stored as repeated
  - `<u32_le packet_size>`
  - `<packet_bytes>`
- a reader advances by reading the frame size and then skipping exactly that many bytes

So:

- `next_offset` is not necessary today
- it may still be added later if a richer on-disk index is introduced

## Current `latest` Data Structure

## `latest` Mode

`latest` stores one current packet per `(robot, channel_id)` key.

### Constraints

- `pdu_def` is mandatory
- only keys defined in `PduDefinition` are accepted
- payload size must match the fixed size from `PduDefinition`
- file layout is fixed at `open()`
- file size is fixed after initialization

### File Layout

`latest` and `queue` do not use the same physical file layout today.

`latest` layout:

```text
+----------------------+
| StorageHeader        |
+----------------------+
| StorageEntry[key_count]
+----------------------+
| packet data area     |
| packet for key 0     |
| packet for key 1     |
| ...                  |
+----------------------+
```

`queue` layout:

```text
+----------------------+
| StorageHeader        |
+----------------------+
| frame 0              |
| <u32_le packet_size> |
| <packet_bytes>       |
+----------------------+
| frame 1              |
| <u32_le packet_size> |
| <packet_bytes>       |
+----------------------+
| ...                  |
+----------------------+
```

`latest` uses a fixed header plus a fixed entry array.

### Header

```cpp
struct StorageHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t mode;
    uint16_t packet_version;
    uint16_t flags;
    uint16_t reserved0;
    uint32_t reserved1;
    uint64_t key_count;
    uint64_t index_offset;
    uint64_t data_offset;
    uint64_t file_size;
};
```

### Entry

```cpp
struct StorageEntry {
    char robot_name[128];
    uint32_t channel_id;
    uint32_t reserved0;
    uint64_t timestamp_ns;
    uint64_t packet_offset;
    uint32_t packet_size;
    uint32_t reserved1;
};
```

### Meaning

For `latest`, each `StorageEntry` means:

- one statically assigned key
- one statically assigned packet slot
- one current timestamp for that key

### Layout

```text
+----------------------+
| StorageHeader        |
+----------------------+
| StorageEntry[key_count]
+----------------------+
| packet data area     |
| packet for key 0     |
| packet for key 1     |
| ...                  |
+----------------------+
```

### Open Behavior

If the file does not exist:

- create `StorageHeader`
- create `StorageEntry[key_count]`
- assign `packet_offset` and `packet_size` from `PduDefinition`
- initialize packet storage
- fix `file_size`

If the file already exists:

- validate header fields
- validate `key_count`
- validate all entries against `PduDefinition`
- validate `packet_offset + packet_size <= file_size`
- validate actual file size against header `file_size`

For `direction: "in"`:

- the file must already exist
- all entries must have `timestamp_ns != 0`
- otherwise `open()` fails

### Read / Write Semantics

- `send(key, ...)`
  - validates key and fixed payload size
  - overwrites the fixed packet slot in place
  - updates `timestamp_ns`
- `recv(key, ...)`
  - reads the current packet for that key
  - returns `HAKO_PDU_ERR_NO_ENTRY` if `timestamp_ns == 0`

## `queue` Mode

`queue` stores all packets in append order.

The current implementation does not yet persist a `StorageEntry[]` index area for queue mode.
Instead, it uses a compact framed stream.

This is an implementation shortcut, not a conceptual rejection of unified metadata.

## Current `queue` Data Structure

### Current File Format

The current implementation uses a storage header followed by repeated frames:

```text
file   := <StorageHeader> record*
record := <u32_le packet_size> <packet_bytes>
```

Where:

- `packet_size` is a 4-byte little-endian unsigned integer
- `packet_bytes` is the encoded raw packet from `DataPacket::encode(...)`
- `StorageHeader.mode == queue` identifies the file as queue storage

### Meaning

For `queue`, each physical record means:

- one appended packet
- one log position in global append order

The current implementation derives the next record position by parsing the current frame size.

### Read Cursors

Two read styles currently exist:

- `recv_next(PduRecord&)`
  - uses one global queue cursor
  - this is the correct replay-oriented path
- `recv(key, ...)`
  - uses per-key filtered cursors
  - this remains only as a compatibility API
  - it should not define queue semantics

### Future Queue Metadata Direction

If queue metadata is made explicit later, the intended direction is still to stay close to the unified model:

- `StorageHeader`
- `StorageEntry[]`
- packet data area

But that richer queue metadata is not implemented yet.

### Open Behavior

If the file does not exist:

- create `StorageHeader`
- set `data_offset = sizeof(StorageHeader)`
- initialize an empty framed log area

If the file already exists:

- validate the storage header
- scan the file from start to end
- validate that each frame length is readable
- validate that each packet decodes successfully

For `direction: "in"`:

- the file must already exist

### Read / Write Semantics

- `send(key, ...)`
  - appends a new framed packet
- `recv_next(PduRecord&)`
  - reads the next record in global append order
  - advances one shared queue cursor
- `recv(key, ...)`
  - compatibility path
  - performs key-filtered reads from per-key offsets
  - should not be treated as the primary replay API

## Direction Semantics

`StorageComm` uses endpoint direction consistently:

- `out`
  - writing is allowed
  - reading is rejected
- `in`
  - reading is allowed
  - writing is rejected
- `inout`
  - both directions are allowed

## Configuration Examples

- [`config/sample/comm/storage_latest_out_comm.json`](/Users/tmori/project/oss/hakoniwa-drone-pro/work/hakoniwa-pdu-endpoint/config/sample/comm/storage_latest_out_comm.json)
- [`config/sample/comm/storage_queue_out_comm.json`](/Users/tmori/project/oss/hakoniwa-drone-pro/work/hakoniwa-pdu-endpoint/config/sample/comm/storage_queue_out_comm.json)

## Status

Implemented:

- file backend
- `latest` open/validate/read/write
- `queue` append/validate/read
- `recv_next(...)` for queue replay order

Planned:

- unified formal metadata naming across `latest` and `queue`
- stronger formal metadata for `queue`
- replay tooling
- documentation and format versioning cleanup
