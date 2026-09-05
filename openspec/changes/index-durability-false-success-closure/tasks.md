# Tasks

Audit provenance: `docs/agent/POST_CERTIFICATION_PRODUCT_AUDIT_2026-09-05.md` (PA-001..PA-006). Design decisions D1-D6 in `design.md`.

## 1. Protocol: terminal scan-failure message

- [x] 1.1 `Commands.h`: add `ScanFailed = 22` (service→engine) + `ScanFailedPayload { VolumeId }`; update the `ScanComplete` contract comment to name `ScanFailed` as the failure terminal. `Commands.cpp`: `ToMessageType` accepts 22.
- [x] 1.2 `Version.h`: `kCurrentProtocolVersion` → `{2, 2}` with the established additive-minor comment; add `ProtocolSupportsScanFailure` (`major > 2 || (major == 2 && minor >= 2)`).
- [x] 1.3 Protocol tests: gate truth table (`{1,99}`/`{2,0}`/`{2,1}` false; `{2,2}`/`{3,0}` true), `ToMessageType(22)`, and `test_command_surface` asserts reply-only `ScanFailed` rejected as a client request.

## 2. Service: never report success for a scan that did not run to completion (PA-001)

- [x] 2.1 `VolumeScanner.cpp`: add `SendScanTerminal(..., success, negotiatedVersion)` — sends `ScanFailed` when supported, falls back to today's `ScanComplete` for legacy peers. Route all four setup-failure paths through it with `success=false`.
- [x] 2.2 Enumeration loop: distinguish `ERROR_HANDLE_EOF` (normal end → `ScanComplete`) from any other `FSCTL_ENUM_USN_DATA` failure and a degenerate <8-byte reply (→ terminal failure). Cancellation/`writeFailed` keep sending no terminal (unchanged).
- [x] 2.3 New test target `ffvolumescanner_terminal_tests`: run `RunVolumeScan` against an unmounted drive letter over an in-process pipe pair; negotiated `{2,2}` must yield `ScanFailed`; negotiated `{2,1}` must yield `ScanComplete` (legacy fallback proof).

## 3. Engine: completion trust model (PA-001 engine side, PA-003, PA-005)

- [x] 3.1 `PrivilegedConnection`: `ScanFailedCallback` + exact-size dispatch case; wire in `VolumeSessionManager::Start` to `OnScanFailed`.
- [x] 3.2 `VolumeSessionManager::VolumeSession`: add `scanBatchFailed`, `reconciliationScan`, `deferredReconciliation` (default false).
- [x] 3.3 `ApplyScanBatch`: on apply failure set `scanBatchFailed` (and diagnostic); success path unchanged (cursor advances only while the flag is clear).
- [x] 3.4 `OnScanComplete` per design D2: ignore when `scanInProgress` already false (diagnostic); paused OR `scanBatchFailed` → no `MarkScanComplete`, abort active pass, keep reconciliation clock and deferred flag; authoritative completion → `MarkScanComplete`, finish pass only when the completed scan was the reconciliation scan, then drain `deferredReconciliation`.
- [x] 3.5 `OnScanFailed`: abort active pass (never finish), never mark complete, never advance the clock; diagnostics per new codes.

## 4. Reconciliation identity (PA-002, PA-004)

- [x] 4.1 `IndexPipeline::AbortReconciliationPass(volume)`: erase pass state, metadata-only update, no removals, no epoch.
- [x] 4.2 `TriggerReconciliation`: session-absent → no-op (also removes the orphan-`StartVolumeScan`-disconnect race); paused → no-op; scan in flight → set `deferredReconciliation` and return (no `Begin`, no clear); otherwise begin pass + mark `reconciliationScan` + send low-priority scan.
- [x] 4.3 `ApplyUsnBatchChanges`: insert included USN upserts into `reconciliationSeen_` while a pass is active, mirroring the MFT path (rule-excluded rows stay excluded via the existing `excluded` predicate at Finish).
- [x] 4.4 `ApplyJournalBatch`: on apply failure, request reconciliation (deferred-safe via 4.2) instead of only logging; position remains monotonic (no replay reordering).

## 5. Startup integrity verdict (PA-006)

- [x] 5.1 `Store`: `IntegrityVerdict { Ok, Corrupt, Inconclusive }`; `RunIntegrityCheckDetailed()` with `ClassifyIntegrityStep(int stepCode)` static classifier (CORRUPT/NOTADB → Corrupt; BUSY/LOCKED/NOMEM/READONLY/INTERRUPT/IOERR/FULL + prepare failure + anything else → Inconclusive; ROW text `"ok"` → Ok; other report row → Corrupt). Keep `bool RunIntegrityCheck()` = `Detailed() == Ok` so existing callers/tests are unchanged.
- [x] 5.2 `Store::Open`: fail without `outIntegrityFailed` unless the verdict is `Corrupt`.
- [x] 5.3 `engine/Main.cpp`: single corruption fallback as today, wrapped in a bounded (≤3 attempts, 500 ms) retry for inconclusive/open failures; delete only on `Corrupt`; diagnostic on inconclusive.
- [x] 5.4 Tests: classifier table unit; healthy DB → `Ok` (existing); truncated/garbage DB → `Corrupt` + existing recovery path; busy/locked DB (exclusive sqlite lock held from a second connection in-process) → `Open` false with `integrityFailed == false` **and files still present**.

## 6. Engine regression tests

- [x] 6.1 `test_volume_session_manager`: `OnScanFailed` → no `MarkScanComplete`, pass aborted, clock unchanged; duplicate/stale completion ignored; paused completion → not marked; batch-failed scan → completion does not mark; `scanBatchFailed` cursor behavior.
- [x] 6.2 `test_volume_session_manager`: journal-resume-invalid during in-flight initial scan → no pass begun, deferred flag set; authoritative completion → `MarkScanComplete` then reconciliation starts (captured low-priority `StartVolumeScan` frame).
- [x] 6.3 `test_index_pipeline`: USN upsert between Begin/Finish survives finalization (durable + projection + snapshot); excluded-by-rule USN upsert still removed at Finish; `AbortReconciliationPass` drops the pass with zero removals and no epoch advance.
- [x] 6.4 Repeated-failure determinism: batch-fail → completion non-authoritative → later clean resume scan completion marks complete again (no sticky poisoning).

## 7. Validation ladder

- [x] 7.1 Focused: `ffprotocol_tests`, `ffcommand_surface_tests`, `ffvolumescanner_terminal_tests`, `ffengine_volume_session_manager_tests`, `ffengine_index_pipeline_tests`, `ffindexstore_store_tests`.
- [x] 7.2 Full Debug CTest (expected 99 + new targets, all green) ×2; release-test full CTest ×1.
- [x] 7.3 Analyze preset warning-clean; ASAN protocol/IPC slice (protocol dispatch touched).
- [x] 7.4 `ffdoc_drift_tests` + OpenSpec strict (`npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict --no-interactive`) + project status/policy gates.
- [x] 7.5 Commit in waves (protocol+service → engine completion → reconciliation → store verdict → tests/docs); push; hosted CI green on final tip; lifecycle entry completed only after final-tip CI.
