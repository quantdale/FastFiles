# Durability re-review

## Review

**Reviewed SHA:** `08be96fa2717cbf5daf9123345a23840c43fb4df`

Inspected saved `repair.diff`, actual repaired Store/manager source, the complete storage-pressure test and VFS wrapper, and manager regressions. No builds, tests, edits, process launches, or ref changes performed. Reported red/green executions are parent-supplied results, not independently rerun here. Concurrent benchmark work is outside this verdict.

## Correct / repaired

- **Rebuild enumeration failure:** now requires terminal `SQLITE_DONE`; readiness publication additionally checks map/entry counts (`src/indexstore/src/Store.cpp:648–658`, `690–716`). The actual VM-interruption regression is nonvacuous (`tests/engine/test_storage_pressure.cpp:248–274`).
- **Reader move assignment:** its call to `Close()` now invalidates both cached readiness fields before installing the incoming snapshot (`Store.cpp:1702–1715`, `1753–1766`). The regression deliberately transfers an incomplete newer snapshot into an already-queried reader (`test_storage_pressure.cpp:277–302`).
- **Direct reconciliation-removal failure:** the manager now retains retry ownership and avoids advancing the successful-reconciliation timestamp while the pass remains active (`src/engine/src/VolumeSessionManager.cpp:1042–1083`). The added regression checks both properties (`tests/engine/test_volume_session_manager.cpp:1258–1292`).
- **FULL injection is real SQLite I/O failure:** the wrapper returns `SQLITE_FULL` from `xWrite`, while forwarding ordinary and WAL shared-memory operations (`tests/indexstore/SqliteWriteFaultVfs.h:116–164`). Cells assert that injection actually fired and mutation did not report success (`test_storage_pressure.cpp:140–194`).
- **Raw-before-repair checks are meaningful:** `Verify` opens SQLite directly, checks canonical/map/FTS coverage and names, and runs FTS integrity checking before `Store::Open` can rebuild anything (`test_storage_pressure.cpp:41–73`).

## Finding: P1 — Missing-table recovery still has a failure/crash window

**Location:** `src/indexstore/src/Store.cpp:799–813`, `950–966`; `629–646`

The missing-structure check repairs the uninterrupted case, but its result is only an in-memory boolean:

1. A missing FTS table is detected.
2. Schema initialization recreates it empty, committing that schema change.
3. The existing ready marker and populated identity map remain intact until the rebuild’s clearing transaction commits.
4. Failure/termination before that commit leaves all required tables present, an empty FTS table, and the old ready marker/map.
5. The next open sees complete schema and matching map/entry counts, skips rebuilding, and can return successful missing search results.

Thus the original missing-table P1 is **not fully closed under interrupted recovery**.

**Minimal regression:**

- Seed `durable_hit.txt`, close, and drop only `entry_name_terms`.
- During the subsequent open, arm WAL `xWrite` FULL when the rebuild reaches `DELETE FROM entry_name_index_map;`. Fail its clearing transaction’s write/commit.
- Before retry, inspect raw state: entries/map retained, terms empty, old marker ready.
- Remove the fault and reopen again. Require candidate coverage, not merely successful open.

A kill after schema recreation but before the clearing commit exercises the same window.

**Smallest fix:** Durably invalidate readiness before recreated structures can become independently committed, or make schema recreation and readiness invalidation atomic.

**Coverage evidence:** Missing-table tests exercise successful recovery only (`test_storage_pressure.cpp:226–246`). The FULL rebuild fixture already sets `state=0`; its migration fixture sets version 2, forcing another rebuild regardless (`196–223`). Neither catches this composition.

Source-demonstrated path; reproduction not executed during this review.

## Finding: P1 — Initial-scan finalization failure drops deferred reconciliation

**Location:** `src/engine/src/VolumeSessionManager.cpp:976–981`, `1046–1076`

`OnScanComplete` captures and clears `deferredReconciliation`. The new finalization-failure retry preserves only `wasReconciliationScan`, not the stronger deferred reconciliation obligation.

For an **initial scan** with a journal-gap reconciliation deferred:

- `wasReconciliationScan == false`;
- `deferred == true`;
- `MarkScanComplete` fails;
- the new branch queues an initial-scan retry and returns before draining `deferred`.

That retry resumes the initial scan’s persisted cursor. Its later completion has no deferred flag, so the explicitly owed full reconciliation has been lost. Ordinary periodic reconciliation may eventually repair the gap, but this is not preservation of the bounded repair intent.

**Minimal regression:** Extend `TestJournalGapDuringInFlightScanDefersUntilCompletion` (`tests/engine/test_volume_session_manager.cpp:1431–1468`) with a transient failure of `MarkScanComplete`. A temporary `BEFORE UPDATE OF scan_complete` trigger using `RAISE(ABORT, ...)` can target the failure without a production seam. Assert that the retained retry is reconciliation-kind and ultimately issues a cursor-free full scan.

**Smallest fix:** Preserve `wasReconciliationScan || deferred` in both queue/floor branches of `retryFinalization`.

This path is introduced by the new early return; previously the deferred reconciliation drain was still reached despite the ignored metadata failure. Source-demonstrated; not runtime-reproduced here.

## Failure-acceptance coverage: remaining narrow gaps

1. **Combine actual FULL with partial reconciliation.** The FULL removal cell calls one 2,000-row `Store::ApplyBatch`; it does not exercise the pipeline’s second 5,000-row chunk. Add one actual WAL-write failure after chunk one commits and assert projection prefix, epoch, retained obligation, and retry. Existing synthetic chunk failure and Store FULL tests provide useful constituent coverage, not that integrated assertion.

2. **Exercise actual manager retry dispatch.** The new manager regression manually marks another scan in progress and calls completion again. It proves finalization behavior, but not issuance of a new full scan through the scheduler. Add capture-pipe verification of the retained retry’s kind, due time, and cursor-free request. Also cover `SetLastReconciliationTime` failure.

3. **Kill boundaries precede the intercepted write.** `onWrite` runs before forwarding `xWrite` (`SqliteWriteFaultVfs.h:116–131`). Phases 1/3 stop at the first WAL write of their chunk, and phase 4 at the first checkpoint database write (`test_storage_pressure.cpp:322–328`). These are genuine external kills with asserted boundaries and committed-prefix counts, but they do not prove recovery after a partially written uncommitted chunk or partially completed checkpoint. Killing before the second matching write would cheaply establish that at least one write was delegated first.

4. **Exception coverage is vacuous in this fixture.** Pressure rows are ASCII; checking for orphan exceptions does not prove preservation of required side-set membership. Add one surrogate-bearing name and verify its expected membership before repair.

5. **Metadata and identity assertions remain limited.** Counts plus map/FTS agreement cannot independently establish that the expected canonical identities/metadata survived. At minimum assert the root survives every kill and all unchanged rows retain the expected original name.

## Restart assumptions

The fresh-object test correctly asserts that the old seen set **does not survive restart**, then explicitly starts a new reconciliation (`test_storage_pressure.cpp:384–395`). It proves convergence **when a complete rescan is supplied**, not automatic startup scheduling.

The six-hour reconciliation interval is not a strict six-hour completion guarantee: scheduling also depends on the ten-minute poll, service availability, pause state, and scan duration (`VolumeSessionManager.cpp:53–54`, `1595–1615`). Retain that qualification. These tests establish process-termination recovery, not machine-power-loss certification.

## Scoped verdict

**Merge verdict: BLOCK.**

The rebuild-enumeration and reader-move findings are closed. Direct manager false-completion handling is substantially improved, and the new failure harness adds genuine coverage. However, interrupted missing-table repair still permits false-ready search results, and the new metadata-failure branch can discard deferred reconciliation. Both have small, focused corrective changes and regression seams.