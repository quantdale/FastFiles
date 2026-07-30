## 1. Durable Storage Schema and Persistence Layer

- [ ] 1.1 Add SQLite as a bundled/statically-linked dependency of `FastFilesEngine`, opened in WAL journal mode with `synchronous=NORMAL`.
- [ ] 1.2 Design and create the `entries` table keyed by (volume identifier, `FileReferenceNumber`), sized to accommodate both NTFS's 64-bit and ReFS's 128-bit file identifiers, with columns for `ParentFileReferenceNumber`, name (plain string, not interned — see design.md D3), size, last-write time, and attribute flags.
- [ ] 1.3 Design and create the `volumes` table keyed by durable volume identifier (volume GUID + cached serial number), with columns for availability status, last-seen timestamp, USN `JournalId`, persisted `ResumeUsn`, scan-progress cursor, and last-reconciliation timestamp.
- [ ] 1.4 Add a stored schema-version marker (e.g., `PRAGMA user_version` or a dedicated metadata row) to support future in-place migrations.
- [ ] 1.5 Implement batched, explicit-transaction writes for ingestion (multi-record commits, not per-record commits).
- [ ] 1.6 Implement scheduled WAL checkpointing (periodic passive checkpoint, size-triggered forced checkpoint).
- [ ] 1.7 Implement startup integrity verification (`PRAGMA integrity_check` or equivalent) with a defined fallback (rebuild from fresh scan) if it fails.

## 2. In-Memory Projection

- [ ] 2.1 Define the fixed-size in-memory entry record layout (`FileReferenceNumber`, `ParentFileReferenceNumber`/dense parent reference, `NameId`, size, timestamps, attributes).
- [ ] 2.2 Implement the interned/deduplicated name pool (contiguous string arena + offset/length table + insertion-time dedup hash lookup).
- [ ] 2.3 Implement the parent→children index used for directory-listing/Column-View-style lookups.
- [ ] 2.4 Implement on-demand full-path reconstruction (parent-chain walk + interned-name concatenation), including defensive cycle detection (stop if a `FileReferenceNumber` is revisited within one walk).
- [ ] 2.5 Pre-size projection allocations using row counts tracked in the `volumes` table metadata to avoid incremental reallocation during bulk rebuilds.

## 3. Startup Rebuild and Incremental Sync

- [ ] 3.1 Implement full projection rebuild from SQLite on `FastFilesEngine` startup, streaming rows in a single pass per volume.
- [ ] 3.2 Publish each volume's rebuilt data as its own snapshot generation as soon as that volume finishes rebuilding, rather than gating on all volumes.
- [ ] 3.3 Implement the ingestion pipeline ordering: commit batch to SQLite first, then apply the same batch to the in-memory projection, then bump/publish the snapshot generation.
- [ ] 3.4 Implement retry-on-failure for a batch whose SQLite commit fails, ensuring the projection is never updated for an uncommitted batch.
- [ ] 3.5 Wire projection updates into the existing double-buffered memory-mapped snapshot and control-pipe notification mechanism from `establish-architecture-foundation`.

## 4. Privileged Service: Real MFT Volume Scanning

- [ ] 4.1 Replace `StartVolumeScan`'s stub response with real raw-volume MFT enumeration using the service's `SeBackupPrivilege`-granted access.
- [ ] 4.2 Implement the MFT attribute allowlist parser: read and forward only `$STANDARD_INFORMATION` and `$FILE_NAME`, explicitly excluding `$DATA` (including resident content) and all other attribute types.
- [ ] 4.3 Implement per-record validation with skip-and-continue semantics for a single malformed/inconsistent record, without aborting the scan.
- [ ] 4.4 Implement batched streaming of scan records to the requesting connection, respecting the existing frame-size and record-count validation rules.
- [ ] 4.5 Extend `StartVolumeScan` to accept an optional, opaque resume cursor; implement resuming enumeration from approximately that position when supplied, and full-from-start enumeration when omitted.
- [ ] 4.6 Confirm scanning succeeds against files exclusively locked by other processes (raw MFT read does not require normal file-sharing-mode access).

## 5. Privileged Service: Real USN Journal Streaming

- [ ] 5.1 Replace `OpenUsnJournal`'s stub response with real `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL`-based journal reads from the supplied `ResumeUsn`.
- [ ] 5.2 Include the volume's current `JournalId` in `OpenUsnJournal` responses so callers can detect a recreated/invalidated journal.
- [ ] 5.3 Apply the same MFT attribute allowlist and per-record validation rules (section 4.2/4.3) to journal change records.
- [ ] 5.4 Implement batched streaming of live change records for the duration of an open journal handle, torn down on `CloseUsnJournal` or disconnect per the existing connection-scoped handle rules.
- [ ] 5.5 Ensure both `StartVolumeScan` and `OpenUsnJournal` respond within normal response time even when no new data is currently available (no indefinite blocking).

## 6. Engine Ingestion Pipeline Orchestration

- [ ] 6.1 Implement the engine-side consumer that receives streamed scan/journal batches from the service and hands them to the ingestion pipeline (section 3).
- [ ] 6.2 Implement filename canonicalization at ingestion (per the foundation change's existing decision), keyed off `FileReferenceNumber`, never off a re-derived path.
- [ ] 6.3 On startup, before calling `StartVolumeScan`, check for a persisted scan-progress cursor per volume and supply it if present.
- [ ] 6.4 Persist the scan-progress cursor after each committed scan batch, and clear/finalize it once a volume's initial scan completes.

## 7. Volume Lifecycle: Disconnect, Reconnect, and Retention

- [ ] 7.1 Map the service's ephemeral, connection-scoped `VolumeId` to the durable volume identifier (volume GUID + serial number) on every `EnumerateVolumes` call.
- [ ] 7.2 Detect a volume's disappearance from `EnumerateVolumes` results and mark its `volumes` row unavailable (with a last-seen timestamp), without deleting its entries.
- [ ] 7.3 Exclude unavailable-volume entries from active/live results by default while keeping them queryable, and ensure no operation on one volume's status can affect another volume's entries.
- [ ] 7.4 Implement an explicit, separate user-triggered action to permanently remove an unavailable volume's index data ("forget this drive"), distinct from the automatic disconnect handling.
- [ ] 7.5 On a volume's reappearance, compare its reported `JournalId` against the persisted one; if it matches, resume incremental journal ingestion from the persisted position.
- [ ] 7.6 If the `JournalId` differs or the persisted resume position falls outside the journal's retained range, mark the volume for reconciliation instead of resuming, and instead of an unconditional full rescan.

## 8. Reconciliation Sweeps

- [ ] 8.1 Implement a per-volume reconciliation sweep that compares persisted entries against current ground truth (fresh scan or filesystem walk) and reports additions, removals, and stale-field updates.
- [ ] 8.2 Implement idle-time/low-I/O-priority scheduling and pacing for reconciliation sweeps so they don't degrade foreground responsiveness.
- [ ] 8.3 Implement periodic automatic scheduling of reconciliation sweeps per available volume (configurable interval), requiring no user-initiated manual rescan.
- [ ] 8.4 Ensure reconciliation sweeps reuse/update existing (volume identifier, `FileReferenceNumber`) rows rather than deleting and recreating a volume's entire entry set.
- [ ] 8.5 Skip scheduling privileged-path reconciliation while the engine is in degraded mode; resume scheduling once the privileged connection returns to `Active`.

## 9. Testing and Validation

- [ ] 9.1 Test crash-recovery: kill `FastFilesEngine` mid-ingestion-batch and verify WAL replay recovers a consistent database on restart with no permanently-lost committed data.
- [ ] 9.2 Test resumable scanning: interrupt an initial volume scan partway through, restart, and verify it resumes from the persisted cursor rather than from zero, with no duplicate or missing entries in the final result.
- [ ] 9.3 Test volume disconnect/reconnect: remove and reattach a volume (or simulate via a test double), verifying entries are retained-but-marked-unavailable while disconnected and correctly resumed or reconciled on reconnect.
- [ ] 9.4 Test journal discontinuity handling: simulate a recreated/invalidated USN journal and verify the engine falls back to reconciliation rather than silently missing changes or blindly resuming.
- [ ] 9.5 Test symlink/junction cycle safety: construct a junction whose target is an ancestor of itself and verify scanning terminates normally with exactly one entry per physical MFT record, and that path reconstruction does not loop.
- [ ] 9.6 Test reconciliation self-correction: simulate a missed create and a missed delete event (e.g., by making a filesystem change while the engine's journal watch is paused) and verify the next sweep corrects both.
- [ ] 9.7 Benchmark in-memory projection RAM usage at a representative large-volume entry count (several million entries) to confirm the interned-name/parent-reference design meets a reasonable memory budget compared to a naive per-entry-full-path baseline.
- [ ] 9.8 Verify MFT attribute allowlisting end-to-end: confirm no `$DATA` bytes (including resident small-file content) ever appear in engine-ingested records or the durable store.
