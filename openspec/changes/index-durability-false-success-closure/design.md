# Design — Index durability false-success closure

## Context and single invariant

The durable index is mutated (and, during reconciliation, pruned) on the strength
of two signals: a scan reaching `ScanComplete`, and a scan-batch `ApplyMftBatch`/
`ApplyUsnBatch` returning `true`. Every reproduced defect is the same class:
**one of these signals is believed when it is not authoritative evidence that the
filesystem was actually enumerated/applied end-to-end.** The repair keeps exactly
one invariant:

> `FinishReconciliationPass` and `MarkScanComplete` are permitted only on a scan
> that (a) actually ran the enumeration to natural end, (b) had every batch
> applied successfully, (c) was not paused, and (d) is the pass's own scan (for
> reconciliation). A terminal failure signal replaces, never supplements, the
> completion signal.

## D1 — Terminal failure is a distinct message, not a status field

`ScanCompletePayload` is currently `{ VolumeId }` and is dispatched by exact-size
check. Two repair shapes were considered:

1. Add a `status` field to `ScanComplete` (in-place schema change). Rejected:
   it silently changes an existing message's wire shape and the strict
   exact-length dispatch, and a stale engine would misread the new length as a
   protocol violation and tear down the connection.
2. **Chosen: additive `ScanFailed = 22` message** `{ VolumeId volumeId; }`, sent
   **instead of** `ScanComplete` when the scan cannot complete. This is the
   established pattern in this codebase (`ScanRecordBatchV2 = 20`,
   `JournalRecordBatchV3 = 21`): distinct type, so no parser can reinterpret an
   old payload, and legacy dispatch keeps its exact-length contract.

The service chooses the terminal message from `negotiatedVersion`. The message is
emitted only when the peer understands it (`ProtocolSupportsScanFailure(v)` =
`v.major > 2 || (v.major == 2 && v.minor >= 2)`); a legacy peer receives today's
`ScanComplete` byte-for-byte, preserving wire compatibility exactly like the
allocated-size V1/V2 fallback.

The new capability requires a minor protocol bump `kCurrentProtocolVersion` →
`{2, 2}`. Minor is purely additive and never gates compatibility
(`Version.h:22-24`), and handshake negotiation takes the per-component minor min
(`ServiceConnection.cpp:141-144`), so a `{2,1}` engine and `{2,2}` service still
interoperate; the service simply won't send `ScanFailed` to the `{2,1}` engine.

### Which scan failures emit ScanFailed (service side)

All paths that currently send a bare `ScanComplete` without enumerating, plus the
mid-enumeration non-EOF failure, are terminal failures:

- `EnsureBackupPrivilegeEnabled()` failure (`VolumeScanner.cpp:145-155`)
- raw-volume open denied (`:167-169`)
- `FSCTL_GET_NTFS_VOLUME_DATA` failure (`:176-181`)
- zero `recordSize`/`bytesPerSector` (`:185-189`)
- mid-scan `FSCTL_ENUM_USN_DATA` failure that is **not** `ERROR_HANDLE_EOF`
  (`:231-239`) — the current code sets `enumerationEnded = true` for every
  failure including genuine I/O faults, then sends `ScanComplete` at `:361`.
  The loop must distinguish `ERROR_HANDLE_EOF` (normal end → `ScanComplete`)
  from any other error (→ `ScanFailed`).

Cancellation (`shouldStop`) and write failure (broken pipe) keep sending **no**
terminal message (unchanged): the connection is going away, and the engine must
not interpret a cancelled scan as either success or a reconciliation-driving
failure.

## D2 — Engine treats completion and failure as mutually authoritative

`VolumeSessionManager` gains per-session scan bookkeeping (guarded by the
existing `mutex_`):

- `scanInProgress` (exists), plus
- `scanBatchFailed`: set when a scan batch's `apply()` returns false; cleared
  when a new initial scan is started.
- `reconciliationScan`: true when the currently in-flight scan was started by
  `TriggerReconciliation` (vs an initial scan).
- `deferredReconciliation`: set when reconciliation was requested while a scan
  was in flight; consumed by the authoritative completion.

`ApplyScanBatch`: on `apply()` failure set `scanBatchFailed` and do **not**
advance the cursor (already true); the flag makes the later completion
non-authoritative.

`OnScanComplete` (authoritative only): if the session is missing → ignore (as
today). If `scanInProgress` was already false when this completion arrives →
**stale/duplicate: ignore with a diagnostic, do not mark, do not finish**
(closes the replayed/duplicate-`ScanComplete` and post-teardown races).
Otherwise:
- If `paused` for the volume → this completion's batches may have been dropped
  (§9.1); do **not** `MarkScanComplete`; if a reconciliation pass is active,
  **abort** it (D3) rather than finishing; do not advance the reconciliation
  clock.
- Else if `scanBatchFailed` → do **not** `MarkScanComplete`; abort any active
  reconciliation pass; a gap exists and the next initial-scan resume (or the
  next reconciliation, which is now permitted because the volume is not falsely
  marked complete) will close it.
- Else (genuinely complete, applied, unpaused): `MarkScanComplete`; and if
  `reconciliationScan` and a pass is active → `FinishReconciliationPass` +
  `SetLastReconciliationTime` (the only place the clock advances). Then drain
  `deferredReconciliation` (a reconciliation requested during this initial scan)
  by starting it now that no scan is in flight.

`OnScanFailed` (new, wired to `ScanFailed`): look up session; clear
`scanInProgress`; if a reconciliation pass is active, **abort** it (never
`FinishReconciliationPass`); do **not** `MarkScanComplete`; clear
`scanBatchFailed`; republish snapshot state; diagnostic. A failed initial scan
leaves the volume un-completed so resume/cadence will retry — it does **not**
wipe anything.

## D3 — Reconciliation pass identity and non-destructive failure

`FinishReconciliationPass` remains the sole place that computes not-seen
removals, but it is now reached only from an authoritative,
`reconciliationScan` completion of the *same* pass. To bind the completion to the
pass that started it, the engine records `reconciliationScan` when
`TriggerReconciliation` begins the pass and requires it before finishing (D2).

New `IndexPipeline::AbortReconciliationPass(volume)`: erases
`reconciliationSeen_[volume]`, records a metadata-only update, and does **not**
compute/apply removals or advance a searchable epoch. It is the correct response
to a scan that never produced authoritative evidence (failure / pause / batch
failure) — the previous partial `reconciliationSeen_` must not be treated as a
complete sweep.

`TriggerReconciliation` while a scan is in flight (initial or a reconciliation
already running): sets `deferredReconciliation` and returns without beginning a
pass (so a mid-scan `JournalResumeInvalid` or JournalId-mismatch event no longer
clears an in-progress pass's seen set mid-scan). Deferred reconciliation is
consumed at the next authoritative completion.

## D4 — Observed-set completeness for USN

`ApplyUsnBatchChanges` mirrors `ApplyMftBatchChanges`' seen-set insertion: while
a reconciliation pass is active, every **included** upserted `EntryId` is
inserted into `reconciliationSeen_`. Deletes are no-ops (the row is being removed
anyway, so it cannot be a false not-seen removal). This is the minimal change to
stop the pass from deleting files the journal already proved exist.

## D5 — Journal batch failure is a reconciliation obligation, not a silent skip

`ApplyJournalBatch` on `apply()` failure currently returns without advancing the
resume position (correct — replaying from the stored position later is
idempotent). This is retained, but is now paired with reconciliation: on failure,
schedule reconciliation for the volume (deferred if a scan is mid-flight), so the
un-applied delta range is eventually closed by a full authoritative sweep rather
than remaining a permanent gap. The resume position is never advanced past
unapplied deltas, so replay can never reorder old deltas over newer ones.

## D6 — Non-destructive integrity verdict

`Store::RunIntegrityCheck` classifies the outcome instead of collapsing it to a
bool:

- `Ok`: a row whose text is `"ok"`.
- `Corrupt`: prepare succeeded, step returned `SQLITE_CORRUPT`/`SQLITE_NOTADB`,
  or the report row is a real corruption report. Only this authorizes the
  documented Task 1.7 delete-and-recreate fallback.
- `Inconclusive`: PRAGMA prepare failed, or step returned a non-ROW/non-
  corruption code (`SQLITE_BUSY`, `SQLITE_LOCKED`, `SQLITE_READONLY*`,
  `SQLITE_IOERR*`, `SQLITE_NOMEM`, `SQLITE_FULL`, …). `Store::Open` fails
  **without** setting `outIntegrityFailed`, so the caller does not delete.

`engine/src/Main.cpp` keeps its behavior only for a genuine corruption verdict
(`integrityFailed == true`). For an open failure with `integrityFailed == false`
(it may be `Inconclusive` or a plain open error) it performs a small bounded
retry (a handful of attempts, short sleeps — never an unbounded loop) before
falling back to the existing "indexing disabled this session, degraded browsing
continues" path, and never deletes files on that path. The `Inconclusive` reason
is recorded via `AppendDiagnostic`.

Rationale: the WAL siblings are deleted **only** for corruption; on an
inconclusive transient fault, deleting the WAL would destroy
committed-but-uncheckpointed transactions, which is exactly the data-loss this
defect caused.

## Testability seams (production defaults unchanged)

- Protocol: unit-assert `ScanFailed = 22`, `ToMessageType(22)`,
  `ProtocolSupportsScanFailure({2,1})==false` / `({2,2})==true`, and that the
  allowlist still rejects reply-only `ScanFailed` as a client request.
- Service scanner failure classification is factored into a small pure helper
  (given `BOOL ok` + `GetLastError()` → enum {Complete, Failed, Cancelled}) so
  the EOF-vs-fault decision is testable without a live volume. `RunVolumeScan`'s
  failure paths send `ScanFailed`; the pure helper is unit-tested.
- Engine: the existing `#define private public` test harness in
  `test_volume_session_manager.cpp` drives `TriggerReconciliation`,
  `OnScanBatch`, `OnScanComplete` directly; new tests drive `OnScanFailed`,
  mid-scan reconciliation deferral, batch-failure→non-authoritative completion,
  stale/duplicate completion rejection, and paused completion. `IndexPipeline`
  tests drive `AbortReconciliationPass` and the USN observed-set insertion with
  the real SQLite store.
- Store: `RunIntegrityCheck` verdict is exercised on a healthy DB (Ok), a
  truncated/garbage file (Corrupt), and — via a test-only injected
  non-corruption step code — Inconclusive; and `Store::Open` on the Inconclusive
  path is asserted to leave the DB file intact.

## Non-goals

No change to the closed client→service command allowlist, connection-scoped
handle rules, framing/length validation, or the delete-on-genuine-corruption
contract. No change to any of the recorded P2 items (PA-010..PA-016) — this
change is bounded to the durable-index false-success cluster only.
