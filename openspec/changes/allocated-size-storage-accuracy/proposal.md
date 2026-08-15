# Allocated (On-Disk) Size Storage Accuracy

## Why

FastFiles' architecture already commits to tracking **both** logical size and
allocated (on-disk) size: `storage-analysis` design D5 specifies that the
treemap is logical-size-driven by default while allocated size is surfaced
alongside it, explicitly because compressed/sparse items (VM images above all)
make the two diverge enormously. The index design (`filesystem-index-store`
via `index-storage-and-scanning` tasks 1.1/2.1) says entries carry a
"persisted size" but the implementation only ever carries one scalar,
`sizeBytes`, from `$FILE_NAME`'s `RealSize`.

The gap is real and user-visible: Storage Analysis cannot show "Size on disk"
anywhere, so a sparse 40 GB `.vhdx` that occupies 8.3 GB on disk is reported
with no way to tell the two apart — a visible regression against the
WizTree-style product this pillar is modeled on.

This change closes that gap end-to-end at close to zero marginal
privileged-access cost, because the NTFS `$FILE_NAME` attribute (already on
the parser's allowlist) carries a cached `AllocatedSize` field directly
adjacent to the `RealSize` field the parser already reads. No `$DATA` access,
no new service command, no new privilege is needed.

## What Changes

- **MFT parser:** `ParsedMftRecord` additionally extracts the cached
  `AllocatedSize` (offset 0x28 of the `$FILE_NAME` value) next to the existing
  `RealSize` (offset 0x30), under the same bounds checks, with the same
  skip-and-continue malformed-record semantics. `$DATA` remains completely
  excluded.
- **Wire protocol (engine ↔ service):** additive, versioned representations —
  new `MftRecordFixedV2` (V1 + allocated size) carried by a new
  `ScanRecordBatchV2` message, and new `UsnDeltaFixedV3` (V2 + allocated size
  + presence bit) carried by a new `JournalRecordBatchV3` message. The
  protocol minor version moves 2.0 → 2.1; peers negotiate and legacy layouts
  (`MftRecordFixedV1`, `UsnDeltaFixedV1`, `UsnDeltaFixedV2`) parse exactly as
  before. When a peer cannot carry allocated size, the service omits
  partial non-delete updates rather than encoding an absent value as a
  destructive zero.
- **Durable store:** schema version 2 with a nullable
  `allocated_size_bytes` column (NULL = unknown; 0 is a valid size) added via
  an in-place `ALTER TABLE` migration of existing version-1 databases; the
  presence-mask semantics ensure a partial USN update without allocated size
  never erases a previously known value.
- **Projection:** `ProjectionEntry` carries `allocatedSizeBytes` + an explicit
  known flag; full scans set both, partial updates preserve the previous value
  when the field is absent, rebuild-from-store restores both.
- **Snapshot/UI transport:** the in-process snapshot format bumps to version 3
  (per-entry `+9` bytes: allocated size + known flag), keeping the existing
  fail-closed version gate. The UI-seam `FolderAggregateResult` payload grows
  the allocated aggregate plus an explicit completeness pair
  (known-count / total-count).
- **Aggregation:** folder aggregates compute the logical total and the
  allocated subtotal with explicit completeness: `Complete` only when every
  descendant's allocated size is known; a partial subtotal is never presented
  as an exact total. Checked arithmetic throughout (saturate at `UINT64_MAX`).
- **Storage Analysis UI:** a "Size on Disk" column in the drill-down list,
  both values in treemap hover metadata with a divergence callout, honest
  "Unknown"/"—" rendering (never `0 B`), complete-only folder aggregates.
  Treemap rectangle area remains driven by logical size (storage-analysis D5);
  no metric toggle is added.
- **Degraded mode:** unchanged logical-size browsing; allocated size is
  honestly unknown (the unprivileged `FindFirstFileEx` path does not surface
  it), represented as unavailable, never fabricated from logical size, and
  never blocking.

## Capabilities

### New Capabilities

(none — this change completes an already-specified gap inside existing
capabilities.)

### Modified Capabilities

- `filesystem-index-store`: entries carry allocated (on-disk) size with
  explicit unknown state through scan, journal, store, projection, and
  snapshot; aggregates expose both totals with completeness semantics.
- `storage-drilldown-and-treemap`: drill-down list and treemap hover/detail
  surfaces expose size-on-disk where known, with unknown and divergence
  rendered honestly.

## Impact

- **Code:**
  - `src/indexsvc/` — `MftParser.{h,cpp}` (allocated extraction),
    `VolumeScanner.{h,cpp}` (version-gated V2 scan batches),
    `UsnJournalReader.cpp` (V3 journal batches + downgrade policy),
    `ServiceConnection.cpp` (negotiated version plumbed to the scanner).
  - `src/protocol/` — `Records.{h,cpp}` (V2/V3 record types, masks,
    parse/serialize, upgrade/downgrade), `Commands.h`
    (`ScanRecordBatchV2`/`JournalRecordBatchV3` message types + headers),
    `Version.h` (2.1 + capability helper), `SnapshotFormat.{h,cpp}` (v3),
    `UiProtocol.{h,cpp}` (aggregate payload + struct version 2).
  - `src/indexstore/` — `EntryRecord.h` (field + mask bit), `Store.{h,cpp}`
    (schema v2 migration, upsert/read/aggregate), `Projection.{h,cpp}`
    (entry field, partial-update semantics, aggregate completeness).
  - `src/engine/` — `PrivilegedConnection.cpp` (new batch message parsing),
    `VolumeSessionManager.{h,cpp}` (batch overloads), `IndexPipeline.{h,cpp}`
    (record conversion, snapshot export, aggregates), `UiServer.{h,cpp}`
    (aggregate result + degraded conversion).
  - `src/ui/` — `EngineClient.{h,cpp}`, `Util.{h,cpp}`,
    `StorageAnalysis.{h,cpp}`, `TreemapView.{h,cpp}`, `WindowShell.cpp`,
    `ffui/SnapshotAggregate.h`.
- **Tests:** `tests/indexsvc/test_mft_parser.cpp`, `tests/protocol/`
  (`test_protocol.cpp`, `test_fuzz.cpp`), `tests/indexstore/`
  (`test_store.cpp`, `test_projection.cpp`), `tests/engine/`
  (`test_index_pipeline.cpp`, `test_volume_session_manager.cpp`), UI layout
  helpers under `tests/ui/`.
- **Docs:** new change under `openspec/changes/allocated-size-storage-accuracy/`;
  canonical specs `filesystem-index-store` and `storage-drilldown-and-treemap`
  synchronized once the change is complete; coordination notes on
  `storage-analysis` task 4.8 and `index-storage-and-scanning` task 1.1 (no
  historical rewriting).
- **Persistence:** in-place schema migration version 1 → 2 (additive nullable
  column); existing rows are unknown until re-observed; WAL/integrity/recovery
  behavior unchanged.

## Non-Goals

- No reading, parsing, or forwarding of `$DATA` (resident or otherwise) —
  security invariant unchanged.
- No new service commands, no generic privileged file-open primitive.
- No treemap logical/allocated metric toggle (explicitly deferred).
- No per-item filesystem probe for allocated size in degraded mode.
- No change to indexed-search candidate wire records (search results keep
  logical size; scope boundary, not an omission in Storage Analysis).
- No change to `storage-overview` OS-sourced capacity figures.
