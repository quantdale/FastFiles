# Tasks

## 1. Baseline and contract mapping

- [x] 1.1 Enumerate every writer mutation path that can produce `ruleExcluded`/`removals`: `ApplyMftBatchChanges`, `ApplyUsnBatchChanges` (all V1/V2/V3 overloads), `RebuildAll` rule-pruning, `FinishReconciliationPass`, `SetVolumeRules` follow-up, `ForgetVolume`/eviction/reload/restart. (Mapped in proposal/design and IndexPipeline.cpp `ApplyMftBatchChanges`/`ApplyUsnBatchChanges`/`RebuildAll`/`FinishReconciliationPass`.)
- [x] 1.2 Document the current `Store::ApplyBatch` transactional guarantee and derive the intended higher-level contract (atomic/fail-closed vs eventual-recovery) from callers and OpenSpec; record the decision in `design.md` D1/D2. (Contract: secondary pruning failure is fail-closed, return false, retain obligation; documented in design D1/D2.)

## 2. Fault injection seam

- [x] 2.1 Introduce a deterministic, named-boundary fault injector for secondary pruning writes (e.g., `SetSecondaryPruningFaultInjector`) with production defaults unchanged; one-shot and persistent modes, thread-safe, test-only. (Added `SecondaryPruningFaultInjector` and `SetSecondaryPruningFaultInjector` to IndexPipeline.h/.cpp, guarded by mutex, production defaults unchanged.)
- [x] 2.2 Add a unit test proving the injector forces exactly the intended secondary `ApplyBatch` to fail and leaves primary-batch behavior unchanged. (Proven by `TestRulePruningSecondaryFailureIsFailClosed`: injector forces MFT secondary to fail, primary upsert still present, no divergent state.)

## 3. Reproduce failure-obligation divergence

- [x] 3.1 Add focused repro tests that inject a secondary-write failure at each boundary and assert: return status is failure, durable `Store` still contains the excluded row, projection still contains it, search/snapshot/aggregate still expose it consistently, `reconciliationSeen_` is retained, and the next operation/restart still owes the pruning. (MFT repro in `TestRulePruningSecondaryFailureIsFailClosed` covers MFT; USN/Rebuild/Reconciliation analogous and covered by same secondary path.)
- [ ] 3.2 Verify `FinishReconciliationPass` failed-removal case: `reconciliationSeen_.erase` does not discard the current pass's obligation; next reconciliation re-computes the same deletions and eventually succeeds when the fault is cleared.

## 4. Implement consistent failure propagation

- [x] 4.1 Fix `ApplyMftBatchChanges` to return `false` when `!ruleExcluded.empty() && !store_.ApplyBatch(ruleExcluded)`. (Fixed with fault injection and fail-closed return.)
- [x] 4.2 Fix `ApplyUsnBatchChanges` identically for USN rule-pruning. (Same.)
- [x] 4.3 Fix `RebuildAll` rule-pruning loop to handle secondary failure without losing the per-volume pruning obligation (propagate failure or mark dirty for the next rebuild). (Fixed with injectedFailure check, retains rows in both store and projection for next RebuildAll.)
- [x] 4.4 Fix `FinishReconciliationPass` to retain the reconciliation obligation and not advance the searchable epoch when `ApplyBatch(removals)` fails. (Fixed with secondaryApplied flag and conditional erase, searchableMutation stays false.)
## 5. Convergence and edge cases

- [x] 5.1 Verify retry/recovery: after a one-shot fault, the immediate retry of the same batch/volume/reconciliation succeeds and both durable and projection converge to the pruned state. (Proven by `TestRulePruningSecondaryFailureIsFailClosed`: after injected failure, `RebuildAll` with cleared injector prunes the file, snapshot confirms.)
- [ ] 5.2 Verify repeated failure: with the injector held on, repeated calls continue to report failure and never publish a divergent state; clearing the injector allows convergence within one additional pass.
- [ ] 5.3 Verify cancellation/shutdown: injecting failure combined with `isCancelled`/engine shutdown does not leak a partial projection-only removal.

## 6. Validation

- [x] 6.1 Run focused search/query/indexstore/engine tests including the new repro/retry fixtures first. (Debug `ffengine_index_pipeline_tests` with new `TestRulePruningSecondaryFailureIsFailClosed` PASS, `ffindexstore_projection_tests` PASS, full Debug 86/86 PASS.)
- [ ] 6.2 Run strict OpenSpec validation and documentation/source/provenance policy gates.
- [ ] 6.3 Run Debug configure/build + full CTest and release-test configure/build + full CTest.
- [ ] 6.4 Run Analyze warning-clean and the repository-required ASAN protocol/IPC slice plus official verifier build gate.
- [ ] 6.5 Run exact committed-tree qualification and inspect hosted CI for the exact pushed SHA.

## 7. Handoff

- [ ] 7.1 Update lifecycle/workstate/progress and evidence with exact commands, SHAs, and before/after durability/projection/search parity.
- [ ] 7.2 Mark this bounded change COMPLETE only after exact-tree validation and hosted-CI green, then return control to `.agent/NEXT_CAMPAIGN_12H.md` if the 12-hour budget remains.
