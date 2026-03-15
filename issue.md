# Issue Notes

## Retrospective Status

This file started as a design scratchpad.
Several Storage-related items are now implemented and documented elsewhere, so this file should be read as:

- historical design discussion
- remaining roadmap
- non-final ideas that were intentionally superseded

### Implemented

- `StorageComm` was added as `protocol: "storage"`
- file backend is implemented
- `storage.mode: "latest"` is implemented
- `storage.mode: "queue"` is implemented
- `latest` uses a fixed file layout initialized at `open()`
- `queue` uses a storage header plus framed append-only records
- `recv_next(PduRecord&)` was added to `Endpoint` / `PduComm`
- `queue` replay order is now represented by `recv_next(...)`
- `hako_pdu_storage_debug` was added as a C++ inspection tool
- the debug tool supports both human-readable output and `--json`
- runnable storage examples were added

### Formalized Elsewhere

These topics now have a proper home outside this issue note:

- storage file format
- storage API model
- `latest` vs `queue` semantics
- debug tool usage

Primary references:

- `docs/storage_comm.md`
- `README.md`
- `examples/README.md`

### Still Open / Future Work

- decide whether `queue` should eventually gain a richer indexed metadata area
- decide whether replay timing control belongs in `StorageComm` or in external tooling
- evaluate `ZenohComm`
- evaluate typed API generation / ROS 2 adapter direction

### Remaining Storage Backlog

The current Storage implementation is already usable, but the following items remain as future work:

- `queue` metadata/index expansion
  - current format is intentionally minimal: `StorageHeader + framed records`
  - future versions may add an index for faster offline lookup or partial scan
- replay tooling
  - `recv_next(...)` now provides the right runtime API shape
  - time-based replay, seek, rate control, and filtering are still open
- tool ecosystem around `--json`
  - the debug tool now exports machine-readable metadata
  - a Python or C++ replay/analyzer tool can be layered on top of that
- clarify the role of `recv(key, ...)` for `queue`
  - it remains as a compatibility path
  - the intended primary API for `queue` is `recv_next(...)`
- fixed-size assumption for `latest`
  - current `latest` deliberately requires `PduDefinition` and fixed payload size
  - if variable-sized snapshot storage is ever needed, it should be designed as a separate extension, not as an accidental relaxation of the current model

### Important Note About Storage Sections Below

Some of the older Storage format proposals below are no longer the current direction.

In particular:

- the earlier generic `StorageFileHeaderV1` / `StorageRecordHeaderV1` proposals were exploratory
- the implemented design is simpler
- the authoritative description is now `docs/storage_comm.md`

## Context

This project already has strong extension points through the separation of:

- `Cache`
- `Comm`
- `PduDefinition`
- `EndpointContainer` / `EndpointCommMultiplexer`

The current discussion identified three major extension directions:

1. Storage-oriented `Comm`
2. Zenoh `Comm`
3. ROS 2 integration based on typed API generation

## Important Constraint

The PDU definition format to treat as the current baseline is the compact format, not the legacy format.

Relevant files:

- `config/sample/comm/hakoniwa/new-pdudef.json`
- `config/sample/comm/hakoniwa/new-pdutypes.json`

Compact format semantics:

- `new-pdudef.json` maps robots to `pdutypes_id`
- `paths[]` points to shared `pdutypes` files
- actual PDU entries live in `new-pdutypes.json`
- each PDU entry includes `channel_id`, `pdu_size`, `name`, and `type`

## Observation

`PduDefinition` is already ROS-oriented at the schema level.

Examples from compact PDU types:

- `geometry_msgs/Twist`
- `sensor_msgs/PointCloud2`
- `hako_msgs/...`
- `hako_mavlink_msgs/...`

This means the project already embeds ROS message type names in the semantic schema. ROS 2 integration is therefore not a foreign add-on; it is a natural extension of the existing model.

## Candidate 1: StorageComm

### Goal

Add a `Comm` implementation that persists PDU traffic to storage.

### Motivation

- audit trail
- replay
- postmortem analysis
- snapshot/history retention

### Likely Design

- add `protocol: "storage"` to comm config
- implement `StorageComm : public PduComm`
- `send()` persists payloads and metadata
- optional `recv()` supports replay or readback

### Storage Modes

`StorageComm` should support two modes aligned with the existing cache vocabulary:

- `latest`
- `queue`

Meaning:

- `latest`
  - persist only the latest value for each `robot + channel_id`
  - suitable for state snapshot, recovery, and current-state inspection
- `queue`
  - persist every record in time order
  - suitable for replay, tracing, and postmortem analysis

### Responsibility Split vs Cache

Even if both `Cache` and `StorageComm` use `latest/queue`, their responsibilities are different:

- `Cache`
  - in-memory runtime behavior
- `StorageComm`
  - persistence behavior

This distinction keeps the model coherent and allows useful combinations such as:

- memory cache `latest` + storage `queue`
- memory cache `queue` + storage `latest`
- no external transport + storage persistence

### Initial Scope

Start with the smallest useful feature set:

- file backend only
- `mode: latest | queue`
- `send()` implemented
- `recv()` initially returns unsupported unless a minimal replay design is agreed
- record at least:
  - timestamp
  - `robot`
  - `channel_id`
  - payload

### Why `queue` Matters

The `queue` mode is the natural basis for log replay.
It should be designed so that later work can add:

- replay readers
- time-based playback
- filtering by robot/channel
- offline inspection tools

### Next Step: Storage Metadata Design

To support robust replay and efficient lookup, the storage file format needs explicit metadata.

This is especially important because:

- `queue` is append/read-sequential
- `latest` requires key lookup
- actual payload size may differ from `pdu_size` in compact PDU definitions
- record-level recovery should still work even if index metadata is damaged

### Storage Format Principle

The file format should have three layers:

1. common file header
2. self-describing record area
3. mode-specific metadata / index area

### Common Header

Fields that should be common to both `queue` and `latest`:

- magic number
- storage format version
- mode (`queue` or `latest`)
- packet format version (`v1` or `v2`)
- header size
- metadata/index offset
- data area offset
- file flags

Optional but likely useful:

- creation timestamp
- last update timestamp
- record count

### Common Record Metadata

Each stored record should be self-describing, regardless of mode.

Minimum required fields:

- record type
- sequence number or timestamp
- key:
  - robot
  - channel_id
- packet size
- payload size
- packet offset or inline packet body

Important point:

The compact PDU definition provides semantic size information, but the actual stored payload size must still be recorded per entry.
This is required for:

- variable-length payloads
- partial payload cases
- future compatibility

### Queue-Specific Metadata

`queue` is naturally sequential, so its metadata can stay small.

Minimum useful metadata:

- entry count
- first record offset
- last record offset

Optional:

- time range
- sequence range

The basic read model is:

- start from the first record offset
- read records in order
- use each record header to advance safely

### Latest-Specific Metadata

`latest` needs a key index.

Minimum useful metadata:

- key list
- for each key:
  - latest record offset
  - latest packet size
  - latest payload size

Optional:

- last update timestamp
- sequence number

The basic read model is:

- load the index
- find the target key
- seek directly to the record offset
- read the record and packet

### Recovery Principle

Index metadata should improve access speed, but records should remain parseable without it.

That means:

- the record area should be recoverable by sequential scan
- the index should be rebuildable from records
- corruption in metadata should not make the entire file unreadable

### Design Direction

The intended direction is:

- `queue`
  - optimized for append and replay
- `latest`
  - optimized for key lookup and state snapshot
- both
  - share a common header and record structure
  - differ mainly in metadata/index strategy

## Proposed Storage File Format v1

This section proposes a concrete binary layout using C/C++-friendly structures.

### Basic Policy

- fixed-size common header at file start
- fixed-size record header before each raw packet
- raw packet bytes are stored as-is
- mode-specific metadata is stored separately
- all integer fields should use little-endian encoding

## Latest v1 Direction

For `latest`, the intended design is now simpler and stricter than the earlier generic proposal:

- `pdu_def` is mandatory
- only keys defined in `PduDefinition` are accepted
- payload size must match the fixed size from `PduDefinition`
- file layout is fixed at `open()`
- file size is fixed after initialization
- if the file already exists, `open()` validates it instead of rebuilding it

### Latest File Layout

```text
+----------------------+
| LatestHeader         |
+----------------------+
| LatestEntry[key_count]
+----------------------+
| packet data area     |
| packet for key 0     |
| packet for key 1     |
| ...                  |
+----------------------+
```

### Latest Header

```cpp
struct LatestHeader {
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

### Latest Entry

```cpp
struct LatestEntry {
    char robot_name[128];    // null-terminated
    uint32_t channel_id;
    uint32_t reserved0;
    uint64_t timestamp_ns;   // last update time, 0 if never updated
    uint64_t packet_offset;  // offset to raw packet bytes
    uint32_t packet_size;    // stored raw packet size
    uint32_t reserved1;
};
```

### Latest Open Rules

If the file does not exist:

- create `LatestHeader`
- create `LatestEntry[key_count]`
- calculate `packet_offset` and `packet_size` from `PduDefinition`
- initialize packet data area
- fix `file_size`

If the file already exists:

- validate header fields
- validate `key_count`
- validate each `LatestEntry` against `PduDefinition`
- validate `packet_offset + packet_size <= file_size`
- validate actual file size against header `file_size`

### Latest Send Rules

- decode the raw packet
- validate key exists in `PduDefinition`
- validate payload size matches fixed `PduDefinition` size
- seek to `packet_offset`
- overwrite the packet bytes in place
- update `timestamp_ns`

## Queue API Gap

The current endpoint API is centered on:

```cpp
recv(const PduResolvedKey& key, ...)
```

This fits `latest`, but it does not match the real semantics of `queue`.

Why:

- `queue` is fundamentally a time-ordered log
- replay should read records in global append order
- key-based `recv(key, ...)` turns queue into a filtered per-key pull API
- that loses the natural replay model

### Direction

Keep the existing API for compatibility, but introduce a queue-oriented receive API.

### Proposed Record Type

```cpp
struct PduRecord {
    PduResolvedKey key;
    uint64_t timestamp_ns;
    std::vector<std::byte> payload;
};
```

Optional future extension:

- raw packet bytes
- packet version
- sequence number

### Proposed Queue API

At the `Endpoint` level:

```cpp
HakoPduErrorType recv_next(PduRecord& out) noexcept;
```

At the `PduComm` level:

```cpp
virtual HakoPduErrorType recv_next(PduRecord& out) noexcept;
```

Default behavior:

- return `HAKO_PDU_ERR_UNSUPPORTED`

### Role Split

- `latest`
  - primary API: `recv(key, ...)`
- `queue`
  - primary API: `recv_next(...)`
- `recv(key, ...)`
  - remains for backward compatibility
  - may be treated as filtered queue read if needed
  - should not define the core replay model

### Compatibility Strategy

Do not remove or change existing `recv(key, ...)`.

Instead:

- add `recv_next(...)`
- let queue-oriented implementations override it
- keep current key-based `recv()` as a compatibility path

### Why This Matters

Without `recv_next(...)`, queue backends cannot cleanly express:

- single global read cursor
- deterministic replay order
- time-ordered event consumption

That limitation is architectural, not just an implementation detail.

### File Layout

```text
+----------------------+
| StorageFileHeaderV1  |
+----------------------+
| Mode Metadata Area   |
+----------------------+
| Record Area          |
|  RecordHeaderV1      |
|  Raw Packet Bytes    |
|  RecordHeaderV1      |
|  Raw Packet Bytes    |
|  ...                 |
+----------------------+
```

### Common Header Structure

```cpp
struct StorageFileHeaderV1 {
    uint32_t magic;               // "HPDS"
    uint16_t version;             // 1
    uint16_t mode;                // 1=queue, 2=latest
    uint16_t packet_version;      // 1=v1, 2=v2
    uint16_t flags;               // reserved

    uint64_t file_size;           // total file size
    uint64_t created_time_ns;     // file creation time
    uint64_t updated_time_ns;     // last update time

    uint64_t metadata_offset;     // start of mode metadata
    uint64_t metadata_size;       // size of mode metadata
    uint64_t record_area_offset;  // start of record area
    uint64_t record_count;        // total record count

    uint8_t reserved[64];
};
```

### Common Record Header Structure

Each stored entry should carry enough information to be parsed without the metadata area.

```cpp
struct StorageRecordHeaderV1 {
    uint32_t magic;               // "HPDR"
    uint16_t version;             // 1
    uint16_t flags;               // reserved

    uint64_t sequence_no;         // monotonically increasing
    uint64_t timestamp_ns;        // record timestamp

    uint64_t record_offset;       // self offset for sanity check
    uint64_t packet_offset;       // offset to raw packet bytes

    uint32_t record_size;         // header + packet bytes
    uint32_t packet_size;         // stored raw packet size
    uint32_t payload_size;        // actual payload size
    uint32_t channel_id;          // resolved key

    char robot_name[128];         // fixed-size robot name
};
```

Notes:

- `packet_size` is the raw framed packet size
- `payload_size` is the actual PDU body size
- `channel_id` + `robot_name` identify the key
- `record_offset` and `packet_offset` help validation and recovery

### Queue Metadata Structure

`queue` metadata can remain small because replay is sequential.

```cpp
struct StorageQueueMetadataV1 {
    uint32_t magic;               // "HPDQ"
    uint16_t version;             // 1
    uint16_t flags;               // reserved

    uint64_t entry_count;         // total entries
    uint64_t first_record_offset; // first record in file
    uint64_t last_record_offset;  // last record in file

    uint64_t first_sequence_no;   // optional but useful
    uint64_t last_sequence_no;    // optional but useful

    uint64_t first_timestamp_ns;  // optional but useful
    uint64_t last_timestamp_ns;   // optional but useful
};
```

Queue read model:

- read `StorageFileHeaderV1`
- read `StorageQueueMetadataV1`
- seek to `first_record_offset`
- iterate record by record using `record_size`

### Latest Metadata Structure

`latest` needs an index table for direct lookup by key.

```cpp
struct StorageLatestMetadataV1 {
    uint32_t magic;               // "HPDL"
    uint16_t version;             // 1
    uint16_t flags;               // reserved

    uint64_t key_count;           // number of indexed keys
    uint64_t index_offset;        // start of index entries
    uint64_t index_entry_size;    // sizeof(StorageLatestIndexEntryV1)
};
```

```cpp
struct StorageLatestIndexEntryV1 {
    char robot_name[128];
    uint32_t channel_id;
    uint32_t reserved0;

    uint64_t latest_record_offset;
    uint32_t latest_record_size;
    uint32_t latest_packet_size;
    uint32_t latest_payload_size;
    uint32_t reserved1;

    uint64_t latest_sequence_no;
    uint64_t latest_timestamp_ns;
};
```

Latest read model:

- read `StorageFileHeaderV1`
- read `StorageLatestMetadataV1`
- load `StorageLatestIndexEntryV1[]`
- locate target key
- seek directly to `latest_record_offset`
- read `StorageRecordHeaderV1` and packet

### Suggested Enum Definitions

```cpp
enum StorageMode : uint16_t {
    STORAGE_MODE_QUEUE = 1,
    STORAGE_MODE_LATEST = 2,
};

enum StoragePacketVersion : uint16_t {
    STORAGE_PACKET_V1 = 1,
    STORAGE_PACKET_V2 = 2,
};
```

### Why This Structure Works

- common header is shared across modes
- common record header makes the record area self-describing
- `queue` stays append-friendly
- `latest` gets direct key lookup
- actual stored size is explicit
- metadata can be rebuilt by scanning records if needed

### Open Design Choices

These still need a decision before implementation:

- exact magic values
- whether `robot_name` should stay fixed-size or become variable-length
- whether metadata lives before or after the record area in the final format
- whether `file_size` is authoritative or advisory
- whether `record_offset` inside each record is necessary or only useful for debugging/recovery

### Open Questions

- file format: JSON Lines vs binary log vs hybrid
- whether timestamps and endpoint metadata are stored together with payload
- whether replay belongs in `Comm` or in a separate tool

## Candidate 2: ZenohComm

### Goal

Add Zenoh as a distributed communication backend.

### Motivation

- wider distributed simulation deployment
- pub/sub style integration
- looser coupling across assets and nodes

### Likely Design

- add `protocol: "zenoh"` to comm config
- implement `ZenohComm`
- define mapping from `PduResolvedKey {robot, channel_id}` to Zenoh key expressions

### Open Questions

- key naming convention
- QoS / delivery semantics mapping
- pub/sub only or query/reply support
- dependency and packaging strategy for Zenoh C++ bindings

## Candidate 3: ROS 2 Typed API Generation

### Goal

Generate typed APIs from compact PDU definitions.

### Motivation

- make PDU semantics first-class in code
- avoid repetitive handwritten serialization glue
- connect Hakoniwa PDUs to ROS 2 topics/messages systematically

### Important Clarification

This is bigger than adding a `ROS2Comm`.

The stronger architecture is:

- `PduDefinition`: semantic schema
- generator: typed API/code generation
- ROS 2 adapter/backend: integration layer using generated types

### Why This Fits the Current Design

The compact format already carries ROS message type names in `new-pdutypes.json`.
That makes `type` a real schema field, not just documentation.

### Likely Outputs

- typed C++ read/write wrappers per PDU
- serialization/deserialization helpers
- optional ROS 2 conversion helpers
- optional topic mapping layer

### Open Questions

- how much type information is available beyond ROS type name + byte size
- versioning of generated code
- handling nested/variable-length ROS message structures
- whether ROS 2 support should live in this repository or a separate adapter repository

## Architectural Takeaway

The project can be viewed as three layers:

1. runtime transport/cache layer
2. semantic schema layer
3. generated typed developer interface layer

Today:

- `Endpoint` covers runtime behavior
- compact `PduDefinition` covers semantic mapping

Possible next step:

- add a generator to promote compact PDU definitions into typed APIs

## Suggested Priority

1. `StorageComm`
2. `ZenohComm`
3. typed API generation / ROS 2 adapter design

Reasoning:

- `StorageComm` is the smallest conceptual extension and immediately useful
- `ZenohComm` is a clean transport addition
- ROS 2 should likely be designed as typed schema utilization, not only as one more `Comm`
