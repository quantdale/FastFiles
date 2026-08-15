# Allocated (On-Disk) Size Storage Accuracy — Design

## Context

`establish-architecture-foundation` locked in the three-process architecture
and the allowlist-only parser (`$STANDARD_INFORMATION` + `$FILE_NAME`, never
`$DATA`). `index-storage-and-scanning` built the engine's durable SQLite store
and compact in-memory projection carrying one size scalar (`sizeBytes`,
populated from `$FILE_NAME` `RealSize`). `storage-analysis` design D5
committed the product to tracking and exposing both logical and allocated
(on-disk) size, calling allocated size out specifically when the two diverge
(compressed files, sparse VM images). Its tasks.md task 4.8 records the gap
explicitly: "allocated size not yet in projection".

This change closes that gap across the whole data path — raw NTFS metadata,
wire protocol, durable store, in-memory projection, snapshot/control surfaces,
folder aggregation, and the Storage Analysis UI — without weakening any
existing security, compatibility, degraded-mode, or performance guarantee.

## Goals / Non-Goals

**Goals:**
- Extract both `AllocatedSize` and `RealSize` from the already-allowlisted
  `$FILE_NAME` attribute with the existing bounds-check discipline.
- Carry allocated size additively through every layer, with an explicit
  "unknown" state that is distinguishable from a genuine zero.
- Never present a partial aggregate or an unknown value as an exact number.
- Preserve degraded mode: logical-size browsing identical to today; allocated
  size honestly unknown; nothing blocks and nothing fabricates.

**Non-Goals:**
- Reading or forwarding `$DATA` in any form; adding service commands or a
  generic file-open primitive.
- A treemap metric toggle (logical vs. allocated area) — left as a follow-up
  per storage-analysis design D5's open question.
- Per-item filesystem probes (handle opens) for allocated size in degraded
  mode — judged inappropriate (see D8).
- Changing indexed-search candidate records or `storage-overview` OS-sourced
  figures.

## Decisions

### D1: Naming and semantics — `logicalSizeBytes` / `allocatedSizeBytes`, unknown is explicit

The existing `sizeBytes` API means **logical size** everywhere it appears
(`$FILE_NAME` `RealSize`); it is retained under that name for compatibility
and documented as logical size. New fields are named
`allocatedSizeBytes` / `allocatedSizeKnown` at every layer:

- `ParsedMftRecord.allocatedSizeBytes`
- `EntryRecord.allocatedSizeBytes` + `EntryRecord.allocatedSizeKnown`
- `ProjectionEntry.allocatedSizeBytes` + `ProjectionEntry.allocatedSizeKnown`
- `SnapshotDirectoryEntry.allocatedSizeBytes` + `.allocatedSizeKnown`
- `DrillItem` / `TreemapNode` equivalents in the UI

**Unknown is never `0`.** Zero is a valid size (zero-byte file). Knowledge is
represented explicitly:

- wire: a presence bit (`kUsnFieldAllocatedSizeBytes`) in the USN field mask;
  the MFT scan record always carries both values (a parsed `$FILE_NAME` always
  contains both), so no mask is needed there — documented, not assumed;
- store: nullable `allocated_size_bytes` column, `NULL` = unknown;
- projection/snapshot/UI: an explicit `allocatedSizeKnown` flag.

**Why:** the repository's own hard rule — "zero is a valid NTFS value and is
never used as an absence sentinel" (`Records.h` on the V2 mask) — must hold
for this field too, or a genuinely empty file and a file whose allocated size
was never observed become indistinguishable, and a later update can turn
unknown into a fabricated zero.

### D2: NTFS source — `$FILE_NAME` cached sizes, never `$DATA`

The `$FILE_NAME` attribute (type 0x30, always resident) carries, at documented
fixed offsets in its value:

| Offset | Field |
|---|---|
| 0x20 | `AllocatedSize` (8) |
| 0x28 | `RealSize` (8) |
| 0x30 | Flags (4) |
| 0x34 | EAs/Reparse (4) |
| 0x38 | Name length in chars (1) |
| 0x39 | Name namespace (1) |
| 0x3A | Name (UTF-16) |

The parser today reads `RealSize` at value+0x30 (48) and the name at
value+0x3A (66), which matches this documented layout (verified against the
NTFS documentation project's `$FILE_NAME` reference and cross-consistent with
the ntfs-3g/TSK layouts, where allocated precedes real size by 8 bytes at the
same absolute offsets). **`AllocatedSize` is therefore at value+0x20 (40)** —
exactly 8 bytes before the already-read `RealSize`.

The existing `valueLength >= 66` gate that protects the name read already
covers offsets 40..56 (needs ≥ 56), so reading `AllocatedSize` adds no new
bounds condition; the read is documented and stays inside the same validated
branch. No `$DATA` attribute is ever opened, parsed, or forwarded; the
allowlist (`0x10`, `0x30`) is unchanged.

**Note on freshness (documented, unchanged behavior):** `$FILE_NAME`'s size
fields are cached and are only refreshed when the file's name changes; on a
live system the authoritative current sizes live in `$STANDARD_INFORMATION`
/ the `$DATA` allocation. The privileged scan path re-reads the MFT record
(`FSCTL_GET_NTFS_FILE_RECORD`) at scan/reconciliation time, which is the
product's existing staleness bound; storage-analysis already labels index
values as "of indexed items". This is the same trust model the existing
`sizeBytes` already uses — the change adds a second number from the same
snapshot, not a stronger guarantee.

### D3: Wire protocol — additive versions gated by a minor protocol bump

**Invariant: no existing packed structure's binary size/layout changes.**
`MftRecordFixedV1`, `UsnDeltaFixedV1`, `UsnDeltaFixedV2` are compatibility
contracts; they are left byte-identical.

New additive shapes (packed, in `Records.h`):

- `MftRecordFixedV2` = V1 layout + trailing `uint64_t allocatedSizeBytes`
  (a parsed `$FILE_NAME` always yields both sizes, so no presence mask).
- `UsnDeltaFixedV3` = V2 layout + trailing `uint64_t allocatedSizeBytes`, with
  a new authoritative presence bit `kUsnFieldAllocatedSizeBytes = 1u << 7`
  added to `kUsnFieldMaskAll`. The V2 mask semantics extend verbatim: the
  service marks the bit present only after obtaining the value from an
  authoritative source (MFT re-enrichment).

New message types (`Commands.h`), mirroring the existing
`JournalRecordBatchV2` pattern exactly:

- `ScanRecordBatchV2` (20) with `ScanRecordBatchV2Header`
  (`ScanRecordBatchHeader` fields + `uint16_t recordFormatVersion = 2`) and
  `MftRecordV2` records.
- `JournalRecordBatchV3` (21) with `JournalRecordBatchV3Header`
  (`JournalRecordBatchV2Header` fields + `uint16_t recordFormatVersion = 3`)
  and `UsnDeltaV3` records.

Version negotiation: `kCurrentProtocolVersion` moves `{2, 0}` → `{2, 1}`.
`IsVersionCompatible` already treats major as the only gate and takes the
per-component min, so:

- 2.1 ↔ 2.1 negotiates 2.1 → new formats flow.
- 2.1 ↔ 2.0 (either direction) negotiates 2.0 → legacy formats flow; a new
  service sends V1 scan batches and V2 journal batches to a 2.0 peer, exactly
  as it does to a major-1 peer today.
- 2.1 ↔ 1.x negotiates 1.x → fully legacy behavior.

A `ProtocolSupportsAllocatedSize(ProtocolVersion)` helper in `Version.h`
(major > 2 || (major == 2 && minor >= 1)) is the single gate the service
checks when choosing wire formats.

**Downgrade policy (never a destructive zero):** for a legacy peer, a V2 USN
record whose mask carries `kUsnFieldAllocatedSizeBytes` cannot be represented.
Following the existing V2→V1 policy (Records.h): a non-delete update lacking
any field the legacy layout must carry is omitted entirely (the
reconciliation path recovers it); delete updates never carry size bits and
always pass. `DowngradeUsnDeltaV3` is a straight copy only when the allocated
bit is absent; the service filters before downgrading. `UpgradeUsnDeltaV2`
produces a V3 record with the allocated bit absent (absence preserved).

### D4: Durable store — schema version 2 via in-place migration

`PRAGMA user_version` moves 1 → 2. `Store::Open` gains one migration step:

- existing version 1: run `ALTER TABLE entries ADD COLUMN
  allocated_size_bytes INTEGER;` (nullable — `NULL` = unknown, 0 = genuine
  zero), then set `user_version = 2`;
- fresh database (version 0): create the version-2 schema directly
  (`CREATE TABLE IF NOT EXISTS ... allocated_size_bytes INTEGER`),
  set `user_version = 2`;
- anything newer: refused exactly as today (fail-closed).

Migration runs inside the same open path that already sets WAL/synchronous
modes; the existing integrity check still runs before any migration. The
`ALTER TABLE` is a metadata-only operation in SQLite — no table rewrite, no
row churn — so it cannot corrupt or resize an existing database and needs no
data backfill: existing rows are `NULL` (unknown) until re-observed by a
scan/reconciliation, which is exactly the honest representation.

`ApplyBatch`'s upsert gains the column with the same mask semantics as every
other field: `allocated_size_bytes = CASE WHEN (? & 128) != 0 THEN
excluded.allocated_size_bytes ELSE entries.allocated_size_bytes END`, with the
insert binding `NULL` when unknown. A partial update lacking the bit
**preserves** the stored value; a full scan sets it; reconciliation corrects
stale values by re-upserting with the bit set.

### D5: In-memory projection — one field set, explicit partial-update semantics

`ProjectionEntry` gains `uint64_t allocatedSizeBytes` + `bool
allocatedSizeKnown` (9 raw bytes; struct padding makes the entry grow 8 bytes,
see Performance). Semantics:

- `Upsert` (full MFT scan records): sets both fields from the record —
  authoritative.
- `UpsertPartial` (USN deltas): `kEntryFieldAllocatedSizeBytes` in the mask
  updates the value; absent → previous value (and known flag) retained;
  brand-new entries get unknown.
- `Remove`: unchanged.
- `RebuildVolumeFromStore`: restores both fields (and the known flag from the
  column's NULL-ness).

A new `kEntryFieldAllocatedSizeBytes = 1u << 7` bit is added to
`EntryFieldMask`; `kEntryFieldMaskAll` grows accordingly. The
`ToEntryFieldMask` mapping in `IndexPipeline` maps the wire bit to the store
bit one-to-one.

### D6: Snapshot and UI-seam transport — versioned, fail-closed

**Snapshot (engine → UI shared mapping):** the private in-process-family
format bumps `kSnapshotFormatVersion` 2 → 3. Per entry the serialized layout
gains `[u64 allocatedSizeBytes][u8 allocatedSizeKnown]` (9 bytes) after
`sizeBytes`; size math, writer, reader, the parser's `kMinBytesPerEntry`
plausibility bound, and payload-capacity calculations update accordingly.
`IsSnapshotFormatCompatible` keeps rejecting anything ≠ 3 (fail-closed, as
before); because both peers ship in one install, a mismatched build is a
deployment error surfaced by the existing handling, never silently
mis-parsed. The 256 MiB snapshot bound is unchanged; with +9 bytes/entry the
entry capacity within the bound drops from ~3.89M to ~3.44M entries per
snapshot (quantified in Performance) — an acceptable, documented trade for
carrying the field, and the bound is a limit, not a reservation.

**UI seam (engine ↔ UI control pipe):** `FolderAggregateResultPayload` grows
`totalAllocatedSizeBytes`, `allocatedBytesKnownCount`, `allocatedBytesTotalCount`
(24 bytes); `kCurrentStructVersion` moves 1 → 2 so a mixed-version pair fails
closed with `RejectedUnsupportedStructVersion` rather than mis-parsing the
fixed payload. The request message is unchanged. `RequestFolderAggregate` /
`FolderAggregateResult` are the only messages whose payloads change.

### D7: Folder aggregation — both totals with explicit completeness

Both aggregate producers (`Projection::GetFolderAggregate`, the subtree walk;
`Store::GetFolderAggregate`, the recursive-CTE fallback for evicted volumes)
return, alongside the existing `itemCount`/`totalSizeBytes`:

- `totalAllocatedSizeBytes` — sum over descendants whose allocated size is
  known;
- `allocatedBytesKnownCount` / `allocatedBytesTotalCount` — the completeness
  pair. `known == total` → `Complete`; `known == 0` → `Unknown`; otherwise
  `Partial`.

**Overflow policy (explicit):** totals saturate at `UINT64_MAX` (checked
addition; the projection walk and the UI's `CurrentParentTotals` already use
this pattern) and the aggregate is marked incomplete on saturation — a
wrapped-looking total must never be presented as exact. The SQLite CTE
aggregate follows SQLite's own `SUM` semantics, mirroring the existing
`size_bytes` handling (documented; SQLite promotes to floating point on
64-bit overflow, so the store-side aggregate is only used as an evicted-volume
fallback and never presented without the projection's saturation
discipline).

**Presentation contract:** the UI renders `Complete` as an exact value,
`Partial` as `"≥ X"` with an explicit known/total qualifier, and `Unknown` as
`"Unknown"` — never a bare partial subtotal masquerading as a total.

### D8: Degraded mode — honest unknown, no probes, no stalls

The unprivileged `FindFirstFileExW` path (`DegradedModeEnumerator`) does not
surface allocated size, and no cheap per-visible-item mechanism exists that is
demonstrably safe: `GetCompressedFileSize` returns compressed/logical size,
not cluster-rounded allocation, and per-file handle opens
(`GetFileInformationByHandleEx(FileStandardInfo)`) on a large directory would
introduce exactly the stalls and scale regressions the design forbids.
Therefore:

- degraded snapshot entries carry `allocatedSizeKnown = false` by
  construction;
- the UI renders `"—"`/`"Unknown"` for size-on-disk in degraded mode —
  never `0 B`, never a substitution of logical size;
- degraded aggregates (`AggregateSnapshotDirectory`) sum known values with the
  same completeness pair; complete only when every descendant is known;
- logical-size browsing, navigation, Home, and the overview are untouched.

This is a deliberate, documented data-availability choice, not a stub: the
privileged path is the only path that can produce allocated size without an
O(volume) crawl, and degraded mode's contract is exactly "partial coverage,
labeled honestly".

### D9: Storage Analysis presentation

- **Drill-down list:** a new "Size on Disk" column. Files: exact allocated
  size when known, `"—"` when unknown. Directories: the folder aggregate's
  allocated total with completeness rendering (exact / `≥ X` / `"—"`).
- **Treemap hover/details:** both values shown —
  `Size: 40.0 GB` / `Size on disk: 8.3 GB`; when known values diverge by more
  than a small epsilon the divergence is called out explicitly (e.g. a
  "(sparse/compressed)" qualifier). Unknown renders as `"Unknown"`.
- **Treemap area:** remains driven by logical size (storage-analysis D5); no
  metric toggle in this change.
- **Folder aggregates:** both metrics displayed with the completeness
  rendering above.
- The "Size" column keeps meaning logical size; nothing relabels logical size
  as "size on disk".

### D10: Security invariants — unchanged and re-verified

- `$DATA` is never parsed, read, or forwarded (allowlist unchanged; the
  `ParsedMftRecord`/`EntryRecord`/wire structs structurally have no payload
  field).
- No new service command; no generic file-open primitive; the closed command
  surface (`Commands.h`) gains only the two additive batch message types that
  are replies to already-existing requests — the same pattern as
  `JournalRecordBatchV2`.
- All offsets, lengths, and arithmetic stay bounds-checked before access; a
  malformed `$FILE_NAME` still isolates that record (skip-and-continue), and
  the whole-record reject path for `$STANDARD_INFORMATION` is untouched.
- Frame-size limits and batch count-vs-bytes cross-validation apply to the new
  batch types exactly as to the existing ones (same parser template).

## Risks / Trade-offs

- **[Risk]** `$FILE_NAME` size fields are cached and can be stale vs.
  `$STANDARD_INFORMATION`. → **Mitigation:** the scan/reconciliation path
  re-reads the MFT record, which is the existing freshness model; the
  allocated value is labeled "of indexed items" like every other index field.
  No new freshness promise is made.
- **[Risk]** Two peers of mixed protocol minor versions silently drop
  allocated size for the downgraded peer's lifetime. → **Mitigation:** the
  negotiated-version gate is explicit and monotonic; omitted non-delete
  updates are recovered by the next reconciliation sweep (the same bound the
  V2→V1 policy already relies on). Never a fabricated zero.
- **[Risk]** Snapshot v3 rejects a v2 snapshot (fail-closed). → **Mitigation:**
  both peers ship in one ACL-locked install; the version gate exists
  precisely to make a mixed-build deployment a visible error rather than
  silent mis-parsing. Documented behavior, not a new hazard.
- **[Risk]** `+8` bytes per projection entry and `+9` per snapshot entry erode
  large-index memory/storage budgets. → **Mitigation:** quantified in
  Performance; the projection stays compact (no per-entry allocation, no
  strings), the snapshot bound stays a limit with headroom, and the store
  column is NULL-compressed for unknown rows (SQLite stores NULLs in the
  page's null bitmap — zero bytes per unknown row).
- **[Risk]** A partial aggregate could be presented as exact. → **Mitigation:**
  D7's completeness pair is plumbed end-to-end and the UI renders Partial
  explicitly; the existing `aggregateFailed`/`calculating` discipline is
  preserved.

## Migration Plan

- **Durable store:** in-place, additive, metadata-only (D4). Version-1
  databases open normally and gain the nullable column; rows stay `NULL`
  (unknown) until re-observed. No rebuild, no data loss, no downgrade path
  needed (an older engine opening a version-2 database refuses it
  fail-closed — but engine and store ship together).
- **Wire/snapshot:** no deployed cross-version peers to migrate (single
  install); additive versions + negotiation keep every documented layout
  parseable forever (fuzz coverage pins this).
- **UI state:** none persisted by storage-analysis itself; the category
  mapping is untouched.

## Validation Plan

- Focused: MFT parser synthetic tests (extraction, equal, sparse-style
  divergence, zeros, maximal values, truncation, malformed lengths, `$DATA`
  exclusion, name/parent/timestamp regressions); protocol round-trip,
  upgrade/downgrade, malformed-input, and fuzz for V1/V2/V3 layouts; store
  migration (v1 → v2 fixture DB built with the raw SQLite API), unknown-vs-zero
  persistence, restart round-trip, batch update; projection full/partial/
  preserve/reconcile/rebuild; snapshot v3 round-trip and v2-fail-closed;
  aggregate completeness (both totals, nested dirs, zero-size entries, unknown
  descendants, overflow saturation); UI helper rendering.
- Suites: `cmake --preset debug` build + `ctest --preset debug`; `release-test`
  preset + CTest; `analyze` preset build (static analysis, warnings-as-errors);
  ASan `ffprotocol_fuzz_tests` build/run; `pwsh ./verify/verify.ps1 build` and
  `doctor`; `git diff --check`; OpenSpec structure/validation conventions
  (proposal/design/tasks/specs completeness, task numbering, `ffdoc_drift_tests`).
- Not attempted (parked campaign): live privileged NTFS runs, production
  signing — out of scope by policy; portable parser/protocol/index tests are
  the primary validation path, with an optional live-volume run used as
  additional evidence only if the environment permits.

## Open Questions

- Whether a future change should add the logical/allocated treemap metric
  toggle (storage-analysis design D5 open question) — explicitly deferred.
- Whether indexed-search candidate records should later carry allocated size —
  tracked as a scope boundary in this change, not a defect.
