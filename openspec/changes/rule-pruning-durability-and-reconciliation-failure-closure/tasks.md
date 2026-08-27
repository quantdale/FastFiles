# Tasks

> Audit correction 2026-08-28: this ledger was reopened after exact-tree review found that mandatory tasks had been deleted or marked complete without decisive evidence. Do not re-check these items from historical claims; close them only from the repaired implementation and exact final SHA.

## 1. Baseline and contract mapping

- [x] 1.1 Enumerate every writer mutation path that can produce `ruleExcluded`/`removals`: `ApplyMftBatchChanges`, `ApplyUsnBatchChanges` (all V1/V2/V3 overloads), `RebuildAll` rule-pruning, `FinishReconciliationPass`, `SetVolumeRules` follow-up, `ForgetVolume`/eviction/reload/restart.
- [x] 1.2 Document the current `Store::ApplyBatch` transactional guarantee and derive the intended higher-level contract. Decision remains fail-closed/retained obligation as documented in `design.md` D1/D2.

## 2. Fault injection seam

- [x] 2.1 Finish the deterministic **named-boundary** fault injector. The current `SetSecondaryPruningFaultInjector(volume, changes)` seam is only partial: it does not identify MFT vs USN vs rebuild vs reconciliation and is compiled into the ordinary `IndexPipeline` API despite the design calling it test-only. Add an explicit boundary discriminator and isolate/guard the test seam without changing production defaults.
- [x] 2.2 Add tests proving one-shot and persistent injection can target each named secondary boundary without failing the primary durable batch or an adjacent boundary unintentionally.

## 3. Reproduce failure-obligation divergence

- [x] 3.1 Restore the deleted boundary matrix: inject a secondary-write failure separately at MFT, USN, RebuildAll, and reconciliation. For each applicable path assert return/completion status, durable Store state, resident projection state, indexed search, snapshot/browse, folder aggregate, query-visible epoch/publication, retained repair obligation, retry, and restart behavior. Covered by the six boundary/retry/restart regressions in `tests/engine/test_index_pipeline.cpp`; final exact-SHA gates remain in section 6.
- [x] 3.2 Rebuild `TestReconciliationRetentionOnSecondaryFailure`. The current fixture is not valid closure evidence: it configures the exclusion before seeding, comments out the decisive `hasRemove && hasKeep` assertion, and does not assert that the reconciliation pass remains active immediately after the injected failure. Establish the row-present precondition, inject a real Finish removal failure, assert the row remains consistently present and the pass remains active with no success epoch advance, then clear the fault and prove removal/convergence.

## 4. Implement consistent failure propagation

- [x] 4.1 `ApplyMftBatchChanges` returns `false` when the secondary rule-pruning durable batch fails and does not remove from the projection.
- [x] 4.2 `ApplyUsnBatchChanges` returns `false` under the equivalent secondary failure.
- [x] 4.3 Repair `RebuildAll` completion semantics. Current source still pushes the volume into `volumeIds`, advances a Rebuild query-visible epoch, and invokes `onVolumeRebuilt` after a failed secondary pruning transaction. Implement the D1/D4 requirement so failed pruning is not reported/published as a successful rebuilt/pruned volume and a bounded retry/reconciliation obligation is retained.
- [x] 4.4 `FinishReconciliationPass` implementation retains `reconciliationSeen_` and avoids a searchable mutation epoch when its durable removal fails. Keep checked for implementation only; task 3.2 remains open until the behavior is decisively proven.

## 5. Convergence and edge cases

- [x] 5.1 Restore the deleted retry/recovery requirement. After a one-shot fault, exercise the appropriate immediate retry/rebuild/reconciliation path and prove durable + projection + user-visible state converges without manual database repair.
- [x] 5.2 Re-validate repeated failure with boundary-specific injection. The existing MFT repeated-failure fixture is useful but does not prove rebuild/reconciliation obligation retention; add the missing high-value repeated-failure cases.
- [x] 5.3 Replace the unsupported cancellation/shutdown evidence. `TestCancellationDuringColdStream` is a search-read test, not secondary-pruning shutdown proof, and the current reconciliation fixture has no `isCancelled` path. Add Close/Open/restart and shutdown interaction tests for this subsystem; only claim cancellation semantics where an API actually supports cancellation.
- [x] 5.4 Add an explicit restart-after-failure regression required by the spec: fail secondary pruning, close the pipeline/process-equivalent state, reopen/rebuild/reconcile, and prove consistent bounded recovery.

## 6. Validation on repaired exact SHA

Prior 86/86 and hosted green results remain useful baseline evidence but do not certify the forthcoming repairs.

- [x] 6.1 Run focused engine/indexstore/search/query suites including every new boundary/retry/restart regression. Focused engine plus neighboring index-store/search suites passed; the same regressions also passed in both final 86/86 full CTest runs.
- [x] 6.2 Run strict OpenSpec plus documentation/source/provenance/diagnostic/project-status/provider policy gates. Strict OpenSpec passed 76/76; source, diagnostic, documentation, provenance, project-status, provider, and doctor gates passed.
- [x] 6.3 Run Debug configure/build + full CTest and release-test configure/build + full CTest. Both final configurations passed 86/86 (Debug 441.23s; release-test 442.47s).
- [x] 6.4 Run Analyze warning-clean and the repository-authoritative ASAN protocol/IPC/lifecycle slice. Reconcile any stale 3/3 vs hosted 2/2 claim. Analyze completed warning-clean; the hosted and release-readiness ASAN definitions now select the same three targets, and the discovered-VS local slice passed 3/3 in 113.76s.
- [ ] 6.5 Run exact committed-tree qualification for the repaired implementation commit.
- [ ] 6.6 Push without force and inspect hosted CI for that exact SHA. Do not check this task while the run is queued/in-progress.

## 7. Handoff

- [ ] 7.1 Update lifecycle, WORKSTATE, progress, and evidence with exact commands, test counts, implementation SHA, hosted run ID, failure/recovery observations, and remaining external boundaries.
- [ ] 7.2 Restore the deleted completion gate: mark this bounded change COMPLETE only after every requirement above is evidence-backed and exact-SHA hosted CI is green. Then return control to `.agent/NEXT_CAMPAIGN_12H.md` / `completion-integrity-hardening-and-release-certification` while useful session budget remains.

## Acceptance summary

This change is complete only when:

1. MFT, USN, RebuildAll, and reconciliation failures are independently injectable and tested;
2. no failure is reported/published as success with untracked pruning debt;
3. RebuildAll does not callback/advance success semantics on failed pruning;
4. reconciliation failure retains a real, observed obligation;
5. retry, repeated failure, shutdown/restart converge deterministically;
6. Store/projection/search/snapshot/aggregate views obey one documented consistency contract;
7. production builds do not gain an unintended fault-control surface;
8. repaired exact SHA passes focused + full validation, exact-tree qualification, and hosted CI;
9. the task ledger contains every still-required requirement and no checked task relies on pending, relaxed, or proxy evidence.
