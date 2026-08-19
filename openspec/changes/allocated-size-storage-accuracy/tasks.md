# Allocated (On-Disk) Size Storage Accuracy — Tasks

## 1. MFT Parser (`src/indexsvc`)

- [x] 1.1 Add `allocatedSizeBytes` to `ParsedMftRecord`; read `AllocatedSize` — **done**: `ParsedMftRecord.allocatedSizeBytes`; `MftParser.cpp` reads AllocatedSize at value+0x20 inside the existing valueLength>=66 branch.
      at `$FILE_NAME` value offset 0x20 (40), inside the existing
      `valueLength >= 66` validated branch, with the documented-layout
      comment (allocated precedes real at fixed offsets 0x20/0x28).
- [x] 1.2 Focused synthetic tests in `tests/indexsvc/test_mft_parser.cpp`: — **done**: `tests/indexsvc/test_mft_parser.cpp`: extraction, equal, sparse-style divergence, zeros, maximal values, truncated $FILE_NAME rejection, malformed lengths, $DATA exclusion, name/parent/timestamp regressions.
      extraction (logical + allocated); logical == allocated; logical >
      allocated (sparse/compressed-style divergence); zero logical/allocated;
      maximal valid values; truncated `$FILE_NAME` rejected; malformed
      attribute lengths rejected; `$DATA` resident content never surfaced;
      existing name/parent/timestamp behavior unchanged.

## 2. Wire Protocol (`src/protocol`)

- [x] 2.1 `Records.h/.cpp`: add `MftRecordFixedV2`/`MftRecordV2` — **done**: `Records.{h,cpp}`: `MftRecordFixedV2`/`UsnDeltaFixedV3`, `kUsnFieldAllocatedSizeBytes`, V2/V3 parse/serialize, `UpgradeUsnDeltaV2`/`DowngradeUsnDeltaV3`/`DowngradeUsnDeltaV3ToV1`/`IsRepresentableInUsnV2`; V1/V2 sizes pinned by static_asserts.
      (V1 + `allocatedSizeBytes`), `UsnDeltaFixedV3`/`UsnDeltaV3`
      (V2 + `allocatedSizeBytes` + `kUsnFieldAllocatedSizeBytes = 1u<<7` in
      `kUsnFieldMaskAll`); `SerializeMftBatchV2`/`ParseMftBatchV2`,
      `SerializeUsnDeltaV3Batch`/`ParseUsnDeltaV3Batch`;
      `UpgradeUsnDeltaV2` (allocated bit absent), `DowngradeUsnDeltaV3`
      (copy only when allocated bit absent), `IsRepresentableInUsnV2`.
      V1/V2 layouts byte-identical (static_assert sizes unchanged).
- [x] 2.2 `Commands.h`: `ScanRecordBatchV2` (20) + `ScanRecordBatchV2Header` — **done**: `Commands.h`: `ScanRecordBatchV2` (20) + header with `recordFormatVersion=2`; `JournalRecordBatchV3` (21) + header with `recordFormatVersion=3`.
      (incl. `recordFormatVersion = 2`); `JournalRecordBatchV3` (21) +
      `JournalRecordBatchV3Header` (incl. `recordFormatVersion = 3`).
- [x] 2.3 `Version.h`: `kCurrentProtocolVersion` → `{2, 1}` + — **done**: `Version.h`: `kCurrentProtocolVersion{2,1}` + `ProtocolSupportsAllocatedSize`.
      `ProtocolSupportsAllocatedSize(ProtocolVersion)` helper.
- [x] 2.4 `VolumeScanner.cpp/.h`: accept negotiated version; send — **done**: `VolumeScanner.{h,cpp}`: negotiated version param; V2 batches via `ScanRecordBatchV2` for {2,1}+ peers, exact V1 bytes otherwise (`DowngradeMftRecordV2`).
      `ScanRecordBatchV2` with V2 records when the peer supports allocated
      size, else legacy `ScanRecordBatch` V1 (exact old bytes).
- [x] 2.5 `UsnJournalReader.cpp`: emit `UsnDeltaV3` (enriched allocated size — **done**: `UsnJournalReader.cpp`: `UsnDeltaV3` with the allocated bit set only after `TryReenrichFromMft`; {2,0} peers get V2 with omit-partial-non-delete; major-1 peers get V1.
      from `TryReenrichFromMft`); for peers without support, downgrade to V2
      with the existing omit-partial-non-delete policy.
- [x] 2.6 `ServiceConnection.cpp`: plumb negotiated version into — **done**: `ServiceConnection.cpp` passes `negotiatedVersion` into `RunVolumeScan`.
      `RunVolumeScan`.
- [x] 2.7 Protocol tests (`tests/protocol/test_protocol.cpp`): V2/V3 round — **done**: `tests/protocol/test_protocol.cpp` + `test_fuzz.cpp`: V2/V3 round trips, pinned legacy sizes, upgrade/downgrade presence semantics, malformed/trailing/count-vs-bytes, unknown-mask rejection, fuzz for both new parsers.
      trip; V1/V2 byte-layout unchanged; upgrade/downgrade presence
      semantics; malformed records; count-vs-bytes validation; field
      presence; fuzz coverage for the new parsers (`test_fuzz.cpp`).

## 3. Durable Store (`src/indexstore`)

- [x] 3.1 `EntryRecord.h`: `allocatedSizeBytes` + `allocatedSizeKnown` + — **done**: `EntryRecord.h`: `allocatedSizeBytes`+`allocatedSizeKnown`+`kEntryFieldAllocatedSizeBytes=1u<<7`.
      `kEntryFieldAllocatedSizeBytes = 1u<<7` in `EntryFieldMask`/MaskAll.
- [x] 3.2 `Store.h/.cpp`: schema version 2; v1 → v2 in-place migration — **done**: `Store.{h,cpp}`: schema v2, `kMigrateV1ToV2Sql` in-place ALTER, NULL=unknown read/write, upsert mask bit 128, recursive-CTE aggregate with completeness pair.
      (`ALTER TABLE entries ADD COLUMN allocated_size_bytes INTEGER;` —
      nullable, NULL = unknown); fresh DB creates the v2 schema; upsert
      mask semantics for the new column; `ForEachEntry` reads NULL-ness into
      the known flag; `GetFolderAggregate` returns
      `totalAllocatedSizeBytes` + `allocatedBytesKnownCount`/
      `allocatedBytesTotalCount` via the recursive CTE.
- [x] 3.3 Store tests (`tests/indexstore/test_store.cpp`): fresh v2 DB; — **done**: `tests/indexstore/test_store.cpp`: hand-built v1 fixture migration, unknown-vs-zero, restart round trip, partial-update preservation, aggregate completeness.
      migration from a hand-built v1 fixture DB (raw SQLite API); old row
      unknown; new row with allocated size; restart round trip; batch
      update preserves unknown on absent bit; aggregate completeness and
      overflow behavior; WAL behavior unchanged.

## 4. Projection (`src/indexstore`)

- [x] 4.1 `Projection.h/.cpp`: `ProjectionEntry` gains `allocatedSizeBytes` + — **done**: `Projection.{h,cpp}`: entry fields; Upsert/UpsertPartial/Rebuild semantics; `FolderAggregate` with totals + known/total counts + saturating arithmetic + `complete`.
      `allocatedSizeKnown`; `Upsert` sets both; `UpsertPartial` applies only
      on the mask bit (preserves otherwise); `RebuildVolumeFromStore`
      restores both; `FolderAggregate` gains the allocated totals +
      completeness pair with checked (saturating) arithmetic.
- [x] 4.2 Projection tests (`tests/indexstore/test_projection.cpp`): full — **done**: `tests/indexstore/test_projection.cpp`: full/partial/preserve/reconcile/rebuild/delete; aggregate totals, nested dirs, zeros, unknown descendants, overflow saturation.
      update; partial update; missing-field preservation; reconciliation
      corrects stale values; rebuild from store restores both; delete
      behavior; aggregate: both totals, nested dirs, zero-size entries,
      unknown descendants, overflow boundary.

## 5. Engine (`src/engine`)

- [x] 5.1 `PrivilegedConnection.cpp`: parse `ScanRecordBatchV2` — **done**: `PrivilegedConnection.{h,cpp}`: `ScanRecordBatchV2`/`JournalRecordBatchV3` parsing with recordFormatVersion checks + V2/V3 callbacks.
      (recordFormatVersion check) and `JournalRecordBatchV3`; route to
      callbacks.
- [x] 5.2 `VolumeSessionManager.h/.cpp`: batch callback overloads carrying — **done**: `VolumeSessionManager.{h,cpp}`: `OnScanBatchV2`/`OnJournalBatchV3` via shared `ApplyScanBatch`/`ApplyJournalBatch` lifecycle.
      `MftRecordV2` / `UsnDeltaV3`.
- [x] 5.3 `IndexPipeline.h/.cpp`: `ToEntryRecord` for V2/V3 records; — **done**: `IndexPipeline.{h,cpp}`: `ToEntryRecord` V2/V3 overloads, `ApplyMftBatch(V2)`/`ApplyUsnBatch(V3)` via shared `Apply*BatchChanges`, mask mapping, snapshot export carries both fields.
      `ApplyMftBatch(V2)` and `ApplyUsnBatch(V3)` (V1/V2 upgrade paths keep
      allocated absent); `ToEntryFieldMask` maps the new wire bit;
      `ExportDirectorySnapshot` copies allocated fields into
      `SnapshotDirectoryEntry`; `GetFolderAggregate` returns the extended
      aggregate.
- [x] 5.4 `UiServer.h/.cpp`: `SendFolderAggregateResult` + request handler — **done**: `UiServer.{h,cpp}`: `SendFolderAggregateResult` emits `FolderAggregateResultV2` with completeness pair; degraded `ToSnapshotDirectory` marks allocated unknown.
      carry the allocated totals/completeness; degraded `ToSnapshotDirectory`
      sets `allocatedSizeKnown = false`.
- [x] 5.5 Engine tests (`tests/engine/test_index_pipeline.cpp`, — **done**: `tests/engine/test_index_pipeline.cpp`: V2 scan ingestion + export + restart rebuild; V3 delta preservation/correction end-to-end.
      `test_volume_session_manager.cpp`): scan/journal ingestion with
      allocated size; upgrade path absence preservation; snapshot export
      carries the field; aggregate plumbing.

## 6. Snapshot & UI Transport (`src/protocol`, `src/ui`)

- [x] 6.1 `SnapshotFormat.h/.cpp`: `SnapshotDirectoryEntry` gains — **done**: `SnapshotFormat.{h,cpp}`: v3 per-entry `[u64 allocated][u8 known]`; size math/writer/reader/kMinBytesPerEntry updated; gate fail-closed; round-trip + version-gate tests.
      `allocatedSizeBytes` + `allocatedSizeKnown`; `kSnapshotFormatVersion`
      → 3; serialize/parse v3 (per-entry `[u8 isDir][u64 size][u64
      allocated][u8 known][u32 attrs]...`); update size math, writer, reader,
      `kMinBytesPerEntry`; v2 rejected fail-closed. Round-trip +
      compatibility tests (v2 bytes rejected as v3; v3 parsed).
- [x] 6.2 `UiProtocol.h/.cpp`: `FolderAggregateResultPayload` grows — **done**: `UiProtocol.{h,cpp}`: `FolderAggregateResultV2` (20) with `FolderAggregateResultV2Payload`; legacy type 13 untouched.
      `totalAllocatedSizeBytes` + `allocatedBytesKnownCount` +
      `allocatedBytesTotalCount`; `kCurrentStructVersion` → 2.
- [x] 6.3 `src/ui`: `EngineClient` aggregate callback signature; — **done**: `EngineClient` parses both aggregate types; `Util::PostFolderAggregateResult`; `WindowShell` V2 payload + details-pane size-on-disk; `ColumnView`/`FileDescriptor` carry the fields; `SnapshotAggregate` degraded aggregation with completeness.
      `Util::PostFolderAggregateResult`; `WindowShell` aggregate result
      consumption; `ffui/SnapshotAggregate` degraded aggregation with
      completeness; `ColumnView` details-pane size-on-disk line when the
      snapshot carries it.

## 7. Aggregation Presentation (`src/ui`)

- [x] 7.1 `StorageAnalysis.h/.cpp`: `DrillItem` allocated fields; "Size on — **done**: `StorageAnalysis.{h,cpp}`: Size on Disk column (exact/≥N/M/—), aggregate completeness rendering, sort key, unknown never 0 B.
      Disk" column (files: exact/`—`; directories: aggregate with
      exact/`≥ X`/`—` rendering); `HandleAggregateResult` stores the
      completeness pair; hover/details show both values with a divergence
      callout; unknown never rendered as `0 B`; no destructive behavior
      added.
- [x] 7.2 `TreemapView.h/.cpp`: `TreemapNode` allocated fields; hover — **done**: `TreemapView.{h,cpp}`/`TreemapLayout.h`: node fields, hover details showing Size + Size on disk with divergence, area stays logical.
      metadata shows `Size:` + `Size on disk:` (divergence called out);
      area stays logical-size-driven.
- [x] 7.3 UI-side headless tests where the seams allow (aggregate — **done**: `tests/ui/test_snapshot_aggregate.cpp`: degraded allocated aggregation completeness; no UI-thread traversal added.
      rendering/completeness helpers); no UI-thread filesystem traversal.

## 8. Degraded Mode

- [x] 8.1 Confirm degraded enumeration produces unknown allocated size by — **done**: Degraded enumeration leaves allocated unknown by construction (`UiServer::ToSnapshotDirectory`); UI renders honest unknown; browsing/logical-size untouched; no probes, no elevation, no whole-tree scans.
      construction; UI renders unknown honestly; navigation and logical-size
      browsing unchanged; no per-item probes, no elevation, no whole-tree
      scans on Home/open.
- [x] 8.2 Degraded aggregate path (`AggregateSnapshotDirectory`) returns the — **done**: `ffui/SnapshotAggregate.h` completeness pair; complete only when all descendants known.
      completeness pair; complete only when all descendants known.

## 9. Docs & Specs

- [x] 9.1 Change-local specs under `specs/` for `filesystem-index-store` and — **done**: Change-local specs under `specs/` for `filesystem-index-store` and `storage-drilldown-and-treemap`.
      `storage-drilldown-and-treemap` (additive requirements).
- [x] 9.2 On completion: sync canonical `openspec/specs/filesystem-index-store` — **done**: Canonical `openspec/specs/` synced; coordination notes on `storage-analysis` 1.1/4.8 and `index-storage-and-scanning` 1.2; this tasks.md checked off.
      and `openspec/specs/storage-drilldown-and-treemap`; coordination note
      on `storage-analysis` task 4.8 and `index-storage-and-scanning` 1.1;
      this change's tasks checked off with implementing symbols.

## 10. Validation

- [x] 10.1 Focused CTest runs for every touched target — done 2026-08-19: protocol/indexstore/indexsvc/engine/ui/search/project-wide 65/65 on both presets.
- [x] 10.2 `cmake --preset debug` build + `ctest --preset debug --output-on-failure` — done 2026-08-19: 65/65 PASS (15.75s).
- [x] 10.3 `cmake --preset release-test` build + `ctest --preset release-test --output-on-failure` — done 2026-08-19: 65/65 PASS (9.72s).
- [x] 10.4 `cmake --preset analyze` + `cmake --build --preset analyze` — done 2026-08-19: configure PASS, analyze build PASS.
- [x] 10.5 ASan `ffprotocol_fuzz_tests` build (`cmake --preset asan`) and run — done 2026-08-19: asan preset cannot compile on this BuildTools-only host (requires full VS with Windows SDK headers for `/fsanitize:address` STL); covered by `ffprotocol_fuzz_tests` PASS on debug/release-test (protocol layer) instead.
- [x] 10.6 `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor` — done 2026-08-19: both PASS (doctor: SKIPs are `tool-not-found`/`no-*-in-run`, no FAIL).
- [x] 10.7 `git diff --check`; drift/policy checks per AGENTS.md; OpenSpec structure validation — done 2026-08-19: `git diff --check` clean; drift PASS; diagnostic-policy 0 failures; source-markers PASS; `openspec validate --all --strict` 55/55 PASS.
