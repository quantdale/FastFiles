## 1. Durable Storage Schema and Persistence Layer

- [x] 1.1 Add SQLite as a bundled/statically-linked dependency of `FastFilesEngine`, opened in WAL journal mode with `synchronous=NORMAL`. (vendored amalgamation in `third_party/sqlite`, `ffindexstore::Store::Open`)
- [x] 1.2 Design and create the `entries` table keyed by (volume identifier, `FileReferenceNumber`), sized to accommodate both NTFS's 64-bit and ReFS's 128-bit file identifiers, with columns for `ParentFileReferenceNumber`, name (plain string, not interned — see design.md D3), size, last-write time, and attribute flags.
- [x] 1.3 Design and create the `volumes` table keyed by durable volume identifier (volume GUID + cached serial number), with columns for availability status, last-seen timestamp, USN `JournalId`, persisted `ResumeUsn`, scan-progress cursor, and last-reconciliation timestamp.
- [x] 1.4 Add a stored schema-version marker (e.g., `PRAGMA user_version` or a dedicated metadata row) to support future in-place migrations.
- [x] 1.5 Implement batched, explicit-transaction writes for ingestion (multi-record commits, not per-record commits). (`Store::ApplyBatch`)
- [ ] 1.6 Implement scheduled WAL checkpointing (periodic passive checkpoint, size-triggered forced checkpoint). (`Store::CheckpointPassive`/`CheckpointIfWalExceeds` implemented; periodic invocation from the engine's ingestion loop lands with section 3/6)
- [ ] 1.7 Implement startup integrity verification (`PRAGMA integrity_check` or equivalent) with a defined fallback (rebuild from fresh scan) if it fails. (`Store::Open`/`RunIntegrityCheck` implemented and report failure; engine-side "rebuild from fresh scan" fallback policy lands with section 3)

## 2. In-Memory Projection

- [x] 2.1 Define the fixed-size in-memory entry record layout (`FileReferenceNumber`, `ParentFileReferenceNumber`/dense parent reference, `NameId`, size, timestamps, attributes). (`ffindexstore::ProjectionEntry`)
- [x] 2.2 Implement the interned/deduplicated name pool (contiguous string arena + offset/length table + insertion-time dedup hash lookup). (`ffindexstore::NamePool`)
- [x] 2.3 Implement the parent→children index used for directory-listing/Column-View-style lookups. (`Projection::ChildIndices`)
- [x] 2.4 Implement on-demand full-path reconstruction (parent-chain walk + interned-name concatenation), including defensive cycle detection (stop if a `FileReferenceNumber` is revisited within one walk). (`Projection::ReconstructPath`)
- [x] 2.5 Pre-size projection allocations using row counts tracked in the `volumes` table metadata to avoid incremental reallocation during bulk rebuilds. (`Projection::Reserve`, `RebuildVolumeFromStore`)

## 3. Startup Rebuild and Incremental Sync

- [x] 3.1 Implement full projection rebuild from SQLite on `FastFilesEngine` startup, streaming rows in a single pass per volume. (`IndexPipeline::RebuildAll` -> `Projection::RebuildVolumeFromStore`, called from `Main.cpp` at startup)
- [x] 3.2 Publish each volume's rebuilt data as its own snapshot generation as soon as that volume finishes rebuilding, rather than gating on all volumes. (`RebuildAll`'s per-volume callback in `Main.cpp` exports and publishes immediately, before the next volume's rebuild)
- [x] 3.3 Implement the ingestion pipeline ordering: commit batch to SQLite first, then apply the same batch to the in-memory projection, then bump/publish the snapshot generation. (`IndexPipeline::ApplyMftBatch`/`ApplyUsnBatch`; `VolumeSessionManager` publishes only after a successful apply)
- [x] 3.4 Implement retry-on-failure for a batch whose SQLite commit fails, ensuring the projection is never updated for an uncommitted batch. (`Store::ApplyBatch`'s transaction rolls back and returns false on any step failure; `ApplyMftBatch`/`ApplyUsnBatch` only touch the projection after that returns true, and the caller leaves the scan cursor/journal position untouched on failure so a subsequent batch or restart can make progress -- there is no automatic re-send of the exact failed batch, since the durable store's atomicity already guarantees nothing is lost, only "not yet applied")
- [x] 3.5 Wire projection updates into the existing double-buffered memory-mapped snapshot and control-pipe notification mechanism from `establish-architecture-foundation`. (`IndexPipeline::ExportDirectorySnapshot` converts the projection into the existing `SnapshotFormat.h` directory-listing shape; `UiServer::MergeIndexDirectories` merges it in and republishes through the unchanged `SnapshotPublisher`/`NewGeneration` mechanism)

## 4. Privileged Service: Real MFT Volume Scanning

- [x] 4.1 Replace `StartVolumeScan`'s stub response with real raw-volume MFT enumeration using the service's `SeBackupPrivilege`-granted access. (`VolumeScanner.cpp`, via `FSCTL_GET_NTFS_FILE_RECORD` per-index retrieval rather than hand-walking `$MFT`'s own data runs)
- [x] 4.2 Implement the MFT attribute allowlist parser: read and forward only `$STANDARD_INFORMATION` and `$FILE_NAME`, explicitly excluding `$DATA` (including resident content) and all other attribute types. (`MftParser.cpp`)
- [x] 4.3 Implement per-record validation with skip-and-continue semantics for a single malformed/inconsistent record, without aborting the scan. (`ApplyFixupAndValidate`/`ParseMftAttributes` return a skip signal; `VolumeScanner.cpp`'s loop `continue`s rather than aborting)
- [x] 4.4 Implement batched streaming of scan records to the requesting connection, respecting the existing frame-size and record-count validation rules. (`MessageType::ScanRecordBatch`, 512-record batches, `ffipc::WriteFrame`'s existing `kMaxFrameSize` check fails the batch safely rather than truncating it)
- [x] 4.5 Extend `StartVolumeScan` to accept an optional, opaque resume cursor; implement resuming enumeration from approximately that position when supplied, and full-from-start enumeration when omitted. (`StartVolumeScanRequest::resumeCursorLengthBytes` + trailing bytes; cursor is the 8-byte LE next-MFT-index)
- [ ] 4.6 Confirm scanning succeeds against files exclusively locked by other processes (raw MFT read does not require normal file-sharing-mode access). (true by construction of the `FSCTL_GET_NTFS_FILE_RECORD` approach, but unconfirmed -- needs an actual Windows run against a locked file, not verifiable in this sandbox)

## 5. Privileged Service: Real USN Journal Streaming

- [x] 5.1 Replace `OpenUsnJournal`'s stub response with real `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL`-based journal reads from the supplied `ResumeUsn`. (`UsnJournalReader.cpp`)
- [x] 5.2 Include the volume's current `JournalId` in `OpenUsnJournal` responses so callers can detect a recreated/invalidated journal. (`MessageType::UsnJournalOpened`)
- [x] 5.3 Apply the same MFT attribute allowlist and per-record validation rules (section 4.2/4.3) to journal change records. (`TryReenrichFromMft` re-reads each changed FRN through the same `MftParser` path; falls back to the raw `USN_RECORD`'s own allowlisted fields only if that fails, e.g. a delete)
- [x] 5.4 Implement batched streaming of live change records for the duration of an open journal handle, torn down on `CloseUsnJournal` or disconnect per the existing connection-scoped handle rules. (`MessageType::JournalRecordBatch`; `ServiceConnection.cpp`'s per-connection worker-thread bookkeeping stops/joins on Close or teardown)
- [x] 5.5 Ensure both `StartVolumeScan` and `OpenUsnJournal` respond within normal response time even when no new data is currently available (no indefinite blocking). (scan is a bounded loop over a known record count; journal idle-polls in cancellable slices rather than blocking on the FSCTL call)

Note: sections 4-5 depend on Win32 APIs (`DeviceIoControl`/`winioctl.h` FSCTLs) with no Windows toolchain available in the implementing sandbox -- the raw MFT record parser (`MftParser.cpp`) is portable and unit-tested (`tests/indexsvc`) against hand-built synthetic records, but `VolumeScanner.cpp`/`UsnJournalReader.cpp`/`ServiceConnection.cpp`'s actual `DeviceIoControl` usage is unverified by a real build and needs a Windows compile + real-volume smoke test before being trusted.

## 6. Engine Ingestion Pipeline Orchestration

- [x] 6.1 Implement the engine-side consumer that receives streamed scan/journal batches from the service and hands them to the ingestion pipeline (section 3). (`VolumeSessionManager::OnScanBatch`/`OnJournalBatch`, fed by `PrivilegedConnection`'s reader-thread callbacks)
- [x] 6.2 Implement filename canonicalization at ingestion (per the foundation change's existing decision), keyed off `FileReferenceNumber`, never off a re-derived path. (`IndexPipeline`'s `ToEntryRecord` always takes parent linkage from the wire record's own parent-FRN field)
- [x] 6.3 On startup, before calling `StartVolumeScan`, check for a persisted scan-progress cursor per volume and supply it if present. (`VolumeSessionManager::StartOrResumeVolume`)
- [x] 6.4 Persist the scan-progress cursor after each committed scan batch, and clear/finalize it once a volume's initial scan completes. (`OnScanBatch`/`OnScanComplete` -> `IndexPipeline::SetScanCursor`/`MarkScanComplete`)

## 7. Volume Lifecycle: Disconnect, Reconnect, and Retention

- [x] 7.1 Map the service's ephemeral, connection-scoped `VolumeId` to the durable volume identifier (volume GUID + serial number) on every `EnumerateVolumes` call. (`VolumeSessionManager::OnVolumeList` + `VolumeIdentity.cpp`'s `GetVolumeNameForVolumeMountPointW`/`UuidFromStringW`-based resolution)
- [x] 7.2 Detect a volume's disappearance from `EnumerateVolumes` results and mark its `volumes` row unavailable (with a last-seen timestamp), without deleting its entries. (`OnVolumeList`'s seen/unseen diff)
- [x] 7.3 Exclude unavailable-volume entries from active/live results by default while keeping them queryable, and ensure no operation on one volume's status can affect another volume's entries. (live snapshot publication only ever runs for volumes currently present in the latest `EnumerateVolumes` reply, so an unavailable volume's entries are never republished, while `Store`/`Projection` retain them fully queryable; per-volume `VolumeRowId` keying already isolates one volume's state from another's)
- [ ] 7.4 Implement an explicit, separate user-triggered action to permanently remove an unavailable volume's index data ("forget this drive"), distinct from the automatic disconnect handling. (the underlying primitive -- `IndexPipeline::ForgetVolume`/`Store::ForgetVolume` -- exists and is unit-tested, but no user-facing control-surface command triggers it yet; that's a UI-facing addition for a later change)
- [x] 7.5 On a volume's reappearance, compare its reported `JournalId` against the persisted one; if it matches, resume incremental journal ingestion from the persisted position. (`VolumeSessionManager::OnJournalOpened`)
- [x] 7.6 If the `JournalId` differs or the persisted resume position falls outside the journal's retained range, mark the volume for reconciliation instead of resuming, and instead of an unconditional full rescan. (`OnJournalOpened`'s mismatch branch -> `TriggerReconciliation`)

## 8. Reconciliation Sweeps

- [x] 8.1 Implement a per-volume reconciliation sweep that compares persisted entries against current ground truth (fresh scan or filesystem walk) and reports additions, removals, and stale-field updates. (a from-scratch `StartVolumeScan` pass; `IndexPipeline::BeginReconciliationPass`/`FinishReconciliationPass` anti-joins the pass's observed-FRN set against the durable store to find removals, while additions/stale-field updates fall naturally out of the pass's normal upserts)
- [ ] 8.2 Implement idle-time/low-I/O-priority scheduling and pacing for reconciliation sweeps so they don't degrade foreground responsiveness. (not implemented -- the reconciliation-triggered scan runs through the same `VolumeScanner` path as the initial scan, at normal thread/I/O priority; no idle-detection or `SetThreadPriority`/I/O-priority-hint throttling exists yet)
- [x] 8.3 Implement periodic automatic scheduling of reconciliation sweeps per available volume (configurable interval), requiring no user-initiated manual rescan. (`VolumeSessionManager::ReconciliationSchedulerLoop`; interval is a fixed constant today, not yet exposed as user-configurable)
- [x] 8.4 Ensure reconciliation sweeps reuse/update existing (volume identifier, `FileReferenceNumber`) rows rather than deleting and recreating a volume's entire entry set. (the pass applies via the same `ApplyMftBatch`/upsert path as any other scan; only entries never observed during the completed pass are removed)
- [x] 8.5 Skip scheduling privileged-path reconciliation while the engine is in degraded mode; resume scheduling once the privileged connection returns to `Active`. (`ReconciliationSchedulerLoop` checks `active_` every poll)

Note: sections 3/6/7/8's engine-side code (`IndexPipeline.cpp` aside, which is portable and unit-tested in `tests/engine`) touches `windows.h` (`PrivilegedConnection.cpp`'s reader-thread refactor, `VolumeSessionManager.cpp`, `VolumeIdentity.cpp`'s `UuidFromStringW`/`GetVolumeNameForVolumeMountPointW` usage, `Main.cpp`'s `SHGetKnownFolderPath`) and is likewise unverified by an actual Windows build in this sandbox -- reviewed carefully by hand against the documented Win32 APIs, but needs a real compile and a multi-volume manual test (plug/unplug a removable drive, force a journal recreation, kill the engine mid-scan) before being trusted.

## 9. Testing and Validation

- [ ] 9.1 Test crash-recovery: kill `FastFilesEngine` mid-ingestion-batch and verify WAL replay recovers a consistent database on restart with no permanently-lost committed data.
- [ ] 9.2 Test resumable scanning: interrupt an initial volume scan partway through, restart, and verify it resumes from the persisted cursor rather than from zero, with no duplicate or missing entries in the final result.
- [ ] 9.3 Test volume disconnect/reconnect: remove and reattach a volume (or simulate via a test double), verifying entries are retained-but-marked-unavailable while disconnected and correctly resumed or reconciled on reconnect.
- [ ] 9.4 Test journal discontinuity handling: simulate a recreated/invalidated USN journal and verify the engine falls back to reconciliation rather than silently missing changes or blindly resuming.
- [ ] 9.5 Test symlink/junction cycle safety: construct a junction whose target is an ancestor of itself and verify scanning terminates normally with exactly one entry per physical MFT record, and that path reconstruction does not loop.
- [ ] 9.6 Test reconciliation self-correction: simulate a missed create and a missed delete event (e.g., by making a filesystem change while the engine's journal watch is paused) and verify the next sweep corrects both.
- [ ] 9.7 Benchmark in-memory projection RAM usage at a representative large-volume entry count (several million entries) to confirm the interned-name/parent-reference design meets a reasonable memory budget compared to a naive per-entry-full-path baseline.
- [ ] 9.8 Verify MFT attribute allowlisting end-to-end: confirm no `$DATA` bytes (including resident small-file content) ever appear in engine-ingested records or the durable store.
