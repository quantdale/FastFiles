# Index durability false-success closure

## Why

The 2026-09-05 post-certification product audit (`docs/agent/POST_CERTIFICATION_PRODUCT_AUDIT_2026-09-05.md`) reproduced six current-tree defects (PA-001..PA-006) in the durable-index ingestion and reconciliation machinery. Every one is a violation of a single product invariant: **the persistent index may treat scan completion or pass completion as authoritative evidence only when the enumeration actually ran to completion and every record of the evidence was actually applied.** Today:

- PA-001 (P1): all scan setup failures (backup-privilege enable, raw-volume open denial, `FSCTL_GET_NTFS_VOLUME_DATA`, degenerate geometry — `src/indexsvc/src/VolumeScanner.cpp:145-189`) and any mid-scan enumeration failure (`:231-243`, non-EOF) terminate with a bare `ScanComplete`, violating the protocol's own contract comment (`Commands.h:33`: "exactly once per scan **that runs to completion**"). During a 6-hour reconciliation pass, `VolumeSessionManager::OnScanComplete` (`VolumeSessionManager.cpp:509-513`) then finishes the pass with a partial/empty observed set and `IndexPipeline::FinishReconciliationPass` (`IndexPipeline.cpp:939-947`) durably removes every not-seen entry — one transient raw-open denial deletes a healthy volume's entire index and advances the cadence clock.
- PA-002 (P2): `ApplyUsnBatchChanges` (`IndexPipeline.cpp:798-859`) never inserts into `reconciliationSeen_` while `ApplyMftBatchChanges` does (`:701-717`). Files upserted by the live journal during any pass are therefore "not seen" and are removed at pass finish — recurring silent staleness on busy volumes for up to one cadence cycle.
- PA-003 (P2): a failed scan-batch apply (`VolumeSessionManager.cpp:483-486`) is never retried or remembered; later batches advance the persisted cursor past the unapplied records and the tail `ScanComplete` still marks the volume durably complete.
- PA-004 (P2): reconciliation can begin mid-scan. The journal triggers `TriggerReconciliation` (`:539`, `:616`) unconditionally — unlike the scheduler which requires `scanComplete` (`:763`). A journal-identity-invalid event during an initial scan clears the pass seen-set (`IndexPipeline.cpp:918-922`) while that scan is streaming; the scan's own completion then finishes the pass with a nearly-empty seen set → mass false removal.
- PA-005 (P2): a scan whose batches were dropped while paused (`:475-480`) is still marked `scanComplete` on completion (`:509`), permanently hiding the dropped range until some future pass.
- PA-006 (P1): `Store::RunIntegrityCheck` (`src/indexstore/src/Store.cpp:294-307`) returns the same verdict for `SQLITE_BUSY`/`SQLITE_IOERR`/prepare failure as for genuine corruption; `src/engine/src/Main.cpp:169-183` then unconditionally deletes `index.db` + `-wal` + `-shm` (discarding committed-but-uncheckpointed transactions). One momentary startup read fault silently destroys the entire persistent index.

No existing test covers any of these: every reconciliation test feeds evidence exclusively via successful `ApplyMftBatch` calls (`tests/engine/test_index_pipeline.cpp:404/520/615/1582/1991/2066`, `test_subtree_gating.cpp:142`, `test_seeded_indexed_mode.cpp:261`, `test_index_pipeline_concurrency.cpp:413/500/744`); the integrity-check tests cover only healthy opens and a genuinely corrupt file.

## What Changes

- Protocol (additive, version-gated): new service→engine terminal message `ScanFailed = 22` sent **instead of** `ScanComplete` whenever a scan could not run to completion. Gated on the minor version bump to `{2,2}` via `ProtocolSupportsScanFailure`, following the exact `ProtocolSupportsAllocatedSize` precedent; peers that negotiated below `{2,2}` keep byte-identical current behavior.
- Engine completion trust: `VolumeSessionManager` records per-session scan ownership and batch-apply failures; a completion is authoritative only for an in-flight, unpaused, fully-applied scan; stale/duplicate terminals are ignored with a diagnostic; the scan cursor freezes at the first un-applied batch; a reconciliation pass is **aborted** (no removals, cadence clock untouched) on failed/paused/failed-batch completion.
- Reconciliation identity: `BeginReconciliationPass` only runs immediately before the pass's own scan starts; journal triggers arriving while any scan is in flight set a deferred flag that fires the pass after the running scan's authoritative completion. New `IndexPipeline::AbortReconciliationPass`.
- Observed-set completeness: USN upserts recorded while a pass is active count as seen (mirroring the MFT path's rule-inclusion gate).
- Journal batch failure: schedules (or defers) a reconciliation pass instead of silently advancing past unapplied deltas; the position remains monotonic so replay can never reorder old deltas over newer state.
- Store startup integrity verdict: `Ok` / `Corrupt` / `Inconclusive` classification (corruption codes vs `BUSY`/`LOCKED`/`IOERR`/`NOMEM`/prepare failure); only `Corrupt` authorizes the documented delete-and-recreate fallback; `Inconclusive` fails the open without deletion; the engine adds a small bounded retry for inconclusive opens.

## Capabilities

### New Capabilities

- `index/scan-completion-truthfulness`: terminal scan failure reporting, authoritative completion, reconciliation pass identity/observed-set completeness, and non-destructive inconclusive integrity verdicts.

### Modified Capabilities

- `privileged-index-service`: scan failure now terminates with `ScanFailed` for `{2,2}`+ peers. (Behavior clarified; no delta file — the new capability spec owns the requirement text.)
- `filesystem-index-store`: integrity-check failure classification (Task 1.7's "never trust a corrupt database" contract now applies only to actual corruption). (Behavior clarified; owned by the new capability spec.)

## Impact

- Durability/correctness: transient failures can no longer delete or falsely complete a volume index; journal-visible files survive reconciliation passes.
- Protocol: one additive service→engine message type + minor version bump; closed command surface unchanged (service→engine replies only — client→service allowlist untouched, `test_command_surface` "exactly six" unchanged); no new privileged operation.
- Security: none — no new surface; failure paths become more truthful, not more permissive.
- Performance: negligible — one extra per-batch flag check; one extra set insert per USN upsert while a pass is active.
- Out of scope (recorded in the audit ledger, not this change): PA-010..PA-016 (UI shutdown UAF, idle-reconnect race, workspace multi-process persistence, failure-details menu, drag-move, clipboard locking, pipe remote-client rejection) and all PA-02x/PA-1xx items.

## Open Questions

- None. The `ScanFailed` shape, version gate, and completion-trust model are fixed in `design.md`.
