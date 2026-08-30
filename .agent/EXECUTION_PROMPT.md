# NEXT CAMPAIGN — FASTFILES (HISTORICAL)

Status: **ARCHIVED — FROZEN 2026-08-30**
Repository: `quantdale/FastFiles`  
Target-Branch: `main`  
Audit-Baseline: `933528ed29c2deac583587bedf3259b8ee617f5d`  
Audit: `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`  
Historical-OpenSpec: `completion-integrity-hardening-and-release-certification`
Completed-Dependency: `rule-pruning-durability-and-reconciliation-failure-closure`
Historical-Follow-On: `completion-integrity-hardening-and-release-certification`

## Archival notice

This prompt is retained as a historical execution record and is not an active
implementation instruction. FastFiles is locally complete with external
production certification pending. Do not resume the dated work below unless a
new reproducible P0/P1, regression, or authorized external certification input
appears; the default next action is **none**.

## Mission

The rule-pruning durability dependency was truthfully completed at
implementation SHA `f65b3bfc770587b4f28f3fa49f3da68faa4142a3`, hosted run
`33122565103`, and 22/22 checked tasks. The completion-integrity hardening
campaign was subsequently executed and is retained here as historical record.
Do not restart either completed campaign without a new current reproducer.

The resulting truth label is **locally complete; external certification pending**;
the remaining boundaries are recorded in the lifecycle and evidence files.

## Historical 2026-08-28 implementation checkpoint

The active rule-pruning dependency is closed at `f65b3bfc770587b4f28f3fa49f3da68faa4142a3` /
run `33122565103`. The current cold-reload candidate is
`ceffa009ef566edfc9895c363a68fdd005ddc0fa`; it fails closed on incomplete
durable projection streams, retains cold retry state, propagates failed rule
enumeration, and refuses unreloaded snapshot export. Focused recovery tests,
Debug/release-test 86/86, Analyze, ASAN 3/3, release tooling, clean candidate
qualification, and the large-index benchmark pass locally. Hosted run
`33129228485` is a green predecessor for `2a3841ceef1b0b99f533ed3bf46d7a63096cc114`
with retained SHA-bound safe release evidence; it is not proof of the current
candidate.

The UIA diagnostic build is clean. Verifier fixes address omitted search
fixtures, duplicate palette toggles, persistent-host scope selection, a shared
Win32 Add-Type guard, and repeated Filters toggling. The latest focused run
still fails at the Filters disclosure, so no live major-journey/UIA PASS is
claimed and the corresponding OpenSpec tasks remain open. Keep production
signing/pins, privileged/clean-host, provider/guest, physical-display, and
foreground-dependent evidence explicitly external.

## Completed wave-1 dependency

Exact source review found that current rule-pruning evidence is incomplete:

1. `IndexPipeline::RebuildAll` still advances rebuild completion/epoch and calls `onVolumeRebuilt` after its secondary pruning durable write fails.
2. `TestReconciliationRetentionOnSecondaryFailure` comments out the decisive row-retention assertion and configures the exclusion early enough that the target can be pruned before the reconciliation failure being claimed.
3. Mandatory tasks were deleted/prematurely checked in the active ledger; the 2026-08-28 planner restored them.
4. The fault injector has no explicit boundary identity for MFT vs USN vs rebuild vs reconciliation.
5. Existing shutdown/restart evidence cites unrelated indexed-search cancellation behavior.
6. The audit-baseline hosted ASAN slice was 2/2 while recent evidence said 3/3; the repaired authority now runs the protocol/IPC/lifecycle 3/3 slice.
7. Hosted release-candidate qualification validates temporary output but the workflow's intended manifest/provenance upload is empty; fix later in the follow-on campaign.

## Mandatory intake

Before product edits:

1. read repository instructions;
2. read `.agent/NEXT_CAMPAIGN_12H.md`;
3. read the 2026-08-28 deep audit;
4. read `openspec/lifecycle.json`;
5. read the complete active rule-pruning proposal/design/spec/tasks;
6. read the queued completion-hardening proposal/design/tasks/spec;
7. fetch/prune and record branch, HEAD, origin/main, ahead/behind, worktree/index/untracked state;
8. inspect hosted CI for the exact live SHA;
9. run repository-native intake/status/doctor if available;
10. preserve unrelated work; never reset/clean/stash it away automatically.

Current source wins if HEAD moved after planning.

## Historical implementation sequence (completed)

### 1. Make secondary pruning failures explicitly targetable

Replace or extend the current test seam with explicit boundary identity, for example:

- MFT rule pruning;
- USN rule pruning;
- RebuildAll rule pruning;
- reconciliation removal.

Keep production behavior unchanged and isolate the fault seam from ordinary production control.

### 2. Fix RebuildAll completion semantics

A failed secondary pruning write MUST NOT result in an externally observable “rebuild succeeded including rule pruning” signal.

At minimum, reason about and test:

- durable rows;
- projection rows;
- query-visible epoch;
- callback invocation;
- retry/dirty obligation;
- later rebuild/reconciliation;
- restart.

Choose the smallest design that satisfies current D1/D4. Do not invent a full transactional rewrite unless required.

### 3. Replace the false-green reconciliation fixture

The test must establish all preconditions explicitly:

- target row exists in durable store/projection before reconciliation;
- target is omitted or excluded in a way that creates a real Finish removal;
- injected fault fires specifically at reconciliation removal;
- failed Finish leaves target visible consistently;
- reconciliation remains active/owed;
- no success epoch is published;
- clearing fault and retrying removes the target;
- pass completes only after durable success.

No commented decisive assertion. No “relaxed to allow current behavior.”

### 4. Prove all four boundary callers

Add distinct tests for MFT, USN, rebuild, and reconciliation. Do not cite “same secondary path” as proof of caller-specific return/callback/epoch behavior.

For each applicable consumer verify:

- caller status/completion;
- Store;
- projection;
- indexed search;
- snapshot/browse;
- folder aggregate;
- epoch/publication;
- retry.

### 5. Prove repeated failure + restart

Add a deterministic fail -> fail -> recover sequence where useful.

Add fail -> Close/Open -> rebuild/reconcile recovery. The spec explicitly requires restart behavior.

Do not use `TestCancellationDuringColdStream` as proxy evidence for this write-path requirement.

## Validation ladder

Run narrow-to-broad on the repaired tree:

1. new boundary-specific rule-pruning tests;
2. full `ffengine_index_pipeline_tests`;
3. affected indexstore/search/query suites;
4. strict OpenSpec;
5. source/diagnostic/doc/provenance/project-status/provider gates;
6. Debug configure/build + full CTest;
7. release-test configure/build + full CTest;
8. Analyze warning-clean;
9. repository-authoritative ASAN slice;
10. verifier status/doctor/build;
11. exact committed-tree qualification;
12. `git diff --check`;
13. final diff review for debug code, weakened tests, TODOs, secrets, accidental artifacts;
14. commit/push without force;
15. inspect hosted CI for the **exact pushed implementation SHA**.

A previously green SHA does not certify code changed after it.

## Historical completion gate

Mark `rule-pruning-durability-and-reconciliation-failure-closure` complete only when:

- its restored task ledger still contains all required work;
- MFT, USN, RebuildAll, reconciliation are independently proven;
- RebuildAll has no false-success publication/callback after failed pruning;
- reconciliation test proves a real retained obligation;
- repeated failure and restart recovery are deterministic;
- production fault surface is appropriately isolated;
- exact repaired SHA passes full validation;
- exact pushed SHA is hosted-green;
- lifecycle/evidence/workstate are synchronized.

Do not check a task whose own evidence says pending/in-progress.

## Historical continuation after the dependency closed

After the rule-pruning change was truthfully completed, the historical session continued through:

`openspec/changes/completion-integrity-hardening-and-release-certification/tasks.md`

That follow-on campaign covers:

- task-ledger truth hardening;
- hosted release manifest/provenance retention;
- sanitizer coverage authority;
- fresh data-integrity/security/concurrency/performance/UI audit;
- bounded repair campaigns for new reproduced material defects;
- final exact-tree release certification.

The session may continue productively for up to approximately twelve hours, but stop early after a fresh audit and certification show no material local work remains.

Final truth label with unavailable external production evidence:

**locally complete; external certification pending**
