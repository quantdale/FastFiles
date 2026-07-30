## Why

`establish-architecture-foundation` deliberately stubbed `FastFilesIndexSvc`'s scan/journal responses and left the durable index storage shape undecided, so it wouldn't block standing up the process architecture. Both `instant-search` and `storage-analysis` need real, incrementally-updated index data to be anything more than a directory walk — this change resolves the storage-shape decision and replaces the stubs with real scanning, journaling, and persistence.

**Sequencing dependency:** this change builds directly on `establish-architecture-foundation`'s process architecture and IPC contract, and should be implemented after that change lands.

## What Changes

- Decide and implement the index storage shape: SQLite (WAL mode) as the durable, crash-safe source of truth, plus a compact in-memory projection (parent-directory-ID references and interned strings rather than repeated full path strings) as what search and storage analysis actually read at runtime — rebuilt from SQLite at startup, kept in sync incrementally thereafter.
- Replace `FastFilesIndexSvc`'s stubbed `StartVolumeScan`/`OpenUsnJournal` responses with real raw MFT parsing (allowlisted `$STANDARD_INFORMATION`/`$FILE_NAME` fields only, per the security model already established in the foundation change) and real USN Change Journal reads, streamed to `FastFilesEngine` as batched records.
- Implement `FastFilesEngine`'s ingestion pipeline: persist scanned/streamed records to SQLite, maintain the in-memory projection, and reconcile the two.
- Implement volume lifecycle handling: a disconnected/unavailable volume (e.g., a removed external drive) retains its last-known index state without corrupting the rest of the database; reconnection triggers reconciliation, not an unconditional full rescan.
- Implement periodic reconciliation sweeps so the index can self-correct after missed filesystem events, application downtime, or other inconsistency, without requiring a full manual rescan.
- Implement resumable indexing: if the application is closed or crashes mid-scan, indexing continues or resumes rather than restarting from zero.

## Capabilities

### New Capabilities
- `filesystem-index-store`: The durable SQLite-backed store and in-memory projection — schema, persistence, incremental updates, reconciliation, resumability, and volume-unavailable handling.

### Modified Capabilities
- `privileged-index-service`: `StartVolumeScan`/`OpenUsnJournal` move from the explicit "not yet implemented" stub responses to real MFT-parsing and USN-journal-reading behavior.
- `index-engine`: Filesystem Snapshot Publication extends from directory-listing-only data (the foundation change's degraded-mode scope) to full persisted, incrementally-updated index data; the engine gains ingestion and reconciliation responsibilities.

## Impact

- New dependency: SQLite (or equivalent embedded database) inside `FastFilesEngine`.
- This is the prerequisite for `instant-search` and `storage-analysis` — both are scoped assuming this change is complete.
- Does not change `FastFiles` (the UI) directly; UI-visible effects (real search results, real storage sizes) land through the `instant-search` and `storage-analysis` changes.
