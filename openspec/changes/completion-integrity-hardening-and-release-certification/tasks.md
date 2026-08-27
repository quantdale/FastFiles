# Tasks

> Dependency: do not mark this umbrella campaign execution-complete until `rule-pruning-durability-and-reconciliation-failure-closure` has been truthfully repaired, validated, pushed, and closed. The first executor action is therefore to finish that active change, not to bypass it.

## 0. Intake and exact-tree reconciliation

- [ ] 0.1 Record campaign start time, target deadline (up to 12 hours), branch, HEAD, origin/main, ahead/behind, worktree/index/untracked state, toolchain, and exact hosted-CI state.
- [ ] 0.2 Read `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`, `.agent/NEXT_CAMPAIGN_12H.md`, lifecycle/status, current active rule-pruning package, this package, current WORKSTATE/progress, and repository instructions.
- [ ] 0.3 Re-enumerate the tracked tree. Compare changed paths/blobs against audited HEAD `933528ed29c2deac583587bedf3259b8ee617f5d`; current code wins over this plan if the tree moved.
- [ ] 0.4 Inspect exact-SHA hosted CI before edits. Record current job/test counts rather than copying historical claims.
- [ ] 0.5 Run repository-native status/doctor/intake as available. Do not discard unrelated work automatically.

## 1. P1 dependency — truthfully finish rule-pruning durability

- [ ] 1.1 Restore/reopen all still-required tasks deleted or prematurely checked in `rule-pruning-durability-and-reconciliation-failure-closure/tasks.md`.
- [ ] 1.2 Replace the generic pruning injector with an explicitly distinguishable boundary contract covering MFT rule prune, USN rule prune, rebuild rule prune, and reconciliation removal, while keeping production defaults inert.
- [ ] 1.3 Encapsulate/guard the fault seam as test-only or otherwise prove the production build does not expose an unnecessary mutable fault surface.
- [ ] 1.4 Repair `RebuildAll`: a failed secondary pruning transaction MUST NOT be published/callbacked as a successfully rebuilt/pruned volume. Retain a bounded retry/reconciliation obligation and avoid success epoch advancement for the failed semantic mutation.
- [ ] 1.5 Rebuild `TestReconciliationRetentionOnSecondaryFailure` with valid preconditions. The target row MUST exist before Finish; no decisive assertion may be commented/relaxed; failed Finish MUST retain the pass and row consistently; successful retry MUST prune it.
- [ ] 1.6 Add a dedicated MFT boundary failure/retry test asserting return status, store state, projection state, snapshot/search/aggregate visibility as applicable, and epoch behavior.
- [ ] 1.7 Add the equivalent USN-specific boundary test; do not cite the MFT fixture as proof.
- [ ] 1.8 Add a RebuildAll boundary test that observes callback/publication/epoch behavior under failure and convergence after the fault clears.
- [ ] 1.9 Add a reconciliation boundary test for first failure, repeated failure, retry, and no false epoch publication.
- [ ] 1.10 Add Close/Open/restart recovery coverage after a failed secondary pruning operation. Prove durable/projection parity and bounded convergence after restart.
- [ ] 1.11 Add shutdown/cancellation interaction coverage only at APIs where those concepts actually exist; remove unsupported claims based on unrelated search cancellation tests.
- [ ] 1.12 Validate exact store/projection/search/snapshot/folder-aggregate consistency for every material failure state, or document why a consumer is not applicable.
- [ ] 1.13 Run focused engine/indexstore/search tests and prove each new regression fails against a deliberately broken variant or otherwise directly asserts the invariant.
- [ ] 1.14 Run full Debug + release-test CTest, Analyze, authoritative ASAN, strict OpenSpec, policy/status/provenance, exact-tree qualification.
- [ ] 1.15 Commit/push the rule-pruning repair, inspect hosted CI for the exact pushed implementation SHA, then update lifecycle/tasks/evidence and close the dependency only if all evidence is complete.

### Phase 1 exit criteria

Proceed only if:

- no active rule-pruning P1 remains;
- all required rule-pruning tasks exist in the ledger and are evidence-backed;
- exact final dependency SHA is green locally-equivalent and hosted;
- no checked task contains pending/in-progress/relaxed evidence.

## 2. Assurance and repository-truth hardening

- [ ] 2.1 Add or extend a ledger-truth regression so a still-required active task cannot silently disappear without a corresponding spec/design revision/evidence disposition.
- [ ] 2.2 Audit all active/external/completed lifecycle entries for checked-task/evidence contradictions introduced since the 2026-08-27 lifecycle campaigns.
- [ ] 2.3 Reconcile `docs/agent/WORKSTATE.md`, `progress.md`, current audit, lifecycle, `.agent/EXECUTION_PROMPT.md`, and planner handoff to the live exact SHA.
- [ ] 2.4 Search current production/test/control-plane source for TODO/FIXME/HACK, commented decisive assertions, disabled tests, placeholder/stub behavior, debug prints, and stale temporary bypasses. Classify every hit; remove/fix only material current issues.
- [ ] 2.5 Confirm current tests are registered and actually executed in intended presets. Identify tests that compile but are omitted from meaningful gates.
- [ ] 2.6 Review timeout/skip/conditional logic for false greens. Do not increase timeouts or disable tests to mask failures.
- [ ] 2.7 Run project-status and release-frontier fixture suites after truth-surface changes.

## 3. CI, sanitizer, and release-evidence closure

- [ ] 3.1 Reproduce the hosted release artifact-retention gap: `Test-ReleaseCandidate.ps1` writes to temporary directories while the workflow uploads `out/release-candidate/**`.
- [ ] 3.2 Change the release qualification interface so safe candidate metadata/provenance is emitted/copied to a deterministic workflow-owned directory. Do not retain secrets or signing material.
- [ ] 3.3 Add a release-tooling regression that fails if a successful qualification produces no retainable candidate manifest/provenance evidence.
- [ ] 3.4 Update CI upload paths to exact produced locations and verify artifact upload is non-empty on the hosted final run.
- [ ] 3.5 Reconcile sanitizer policy. Determine whether `ffipc_listener_lifecycle_tests` is supported under ASAN; if yes, restore it to the authoritative hosted slice. If not, document the incompatibility and provide equivalent lifecycle memory-safety coverage.
- [ ] 3.6 Remove stale “ASAN 3/3” or “2/2” claims that do not match the chosen authority.
- [ ] 3.7 Review GitHub Actions warnings/deprecations. Upgrade only supported action/runtime usage that presents real future-breakage risk.
- [ ] 3.8 Run unsigned release-candidate qualification twice and confirm deterministic archive/checksum/provenance behavior remains intact.
- [ ] 3.9 Exercise release-readiness report/composition. Preserve `BLOCKED_EXTERNAL` for unavailable signing/pins/clean-host/elevated/display prerequisites.

### Phase 3 exit criteria

- hosted release qualification retains non-empty safe evidence;
- sanitizer coverage has one documented authoritative definition;
- no release script infers PASS for tests/OpenSpec/signing it did not run;
- external prerequisites remain truthful.

## 4. Fresh repository-wide adversarial audit

Re-audit the live post-fix tree. Historical findings are hypotheses only.

### 4.1 Correctness and data integrity

- [ ] 4.1.1 Trace every durable mutation in `Store` and `IndexPipeline`: primary writes, secondary cleanup, rule changes, reconciliation, forget, metadata/cursor/journal updates, recovery/rebuild.
- [ ] 4.1.2 Look for ignored return values, partial projection application, epoch/publication before durable success, stale retry obligations, and restart non-equivalence.
- [ ] 4.1.3 Re-audit file operations/conflict resolution for partial failures, cancellation, overwrite/rename/move/delete edge cases, and recovery/reporting.
- [ ] 4.1.4 Re-audit snapshot/export/aggregate consistency under evicted/cold/partial-parent-chain states.

### 4.2 Security, privilege, IPC, trust

- [ ] 4.2.1 Re-enumerate privileged index-service and engine/UI command surfaces; reject unexpected commands or permissive defaults.
- [ ] 4.2.2 Re-audit peer authentication/image matching, pipe ACLs, service/task rights, opaque-handle ownership, write deadlines, frame/length bounds, and malformed protocol handling.
- [ ] 4.2.3 Re-audit DLL loading/search path, package trust, Authenticode/pins, installer source policy, and production-debug/unsigned bypass prevention.
- [ ] 4.2.4 Run existing security integration/fuzz fixtures and add regression only for a reproduced gap.

### 4.3 Concurrency, lifetime, shutdown

- [ ] 4.3.1 Re-audit `IndexPipeline`, `Store` read sessions, projection/name storage, watcher/service connections, UI server, and background schedulers for lock-order, shutdown, cancellation, use-after-lifetime, and stale-generation risks.
- [ ] 4.3.2 Confirm long-running search/path/SQLite reader work remains outside the mutation critical section where required.
- [ ] 4.3.3 Re-check the previously noted `Store::cachedPageSize_` mutable-state concern only if current call graph permits concurrency; otherwise document it as non-actionable and leave it alone.
- [ ] 4.3.4 Add stress/regression coverage for any reproduced race/lifetime issue.

### 4.4 Performance and resource bounds

- [ ] 4.4.1 Re-run representative large-index/search benchmarks; compare with checked-in baselines and recent indexed-search optimization evidence.
- [ ] 4.4.2 Check state-lock wait/hold, candidate/materialization counters, projection memory bounds, compaction/eviction/reload, SQLite WAL/checkpoint behavior, and startup/rebuild costs.
- [ ] 4.4.3 Inspect UI renderer/icon/preview/treemap/search refresh paths for unnecessary redraws, allocations, I/O, polling, or background work.
- [ ] 4.4.4 Do not optimize a path without a measured cost and correctness-safe acceptance gate.

### 4.5 UI/UX, accessibility, compatibility

- [ ] 4.5.1 Trace Home -> browse -> deep navigation -> tabs/dual pane -> search -> preview/properties -> file operation -> settings/storage flows end-to-end.
- [ ] 4.5.2 Exercise keyboard-only and UIA semantics, focus/tab order, selection identity, dynamic events, narrow/wide layouts, long names, Unicode, empty directories/searches, error/disabled states.
- [ ] 4.5.3 Validate degraded/indexed/offline/unavailable transitions and reconnect/restart behavior.
- [ ] 4.5.4 Treat physical-display/direct-render validation as external when the environment cannot provide it; do not substitute screenshots generated from a different path as PASS.

### 4.6 Build, dependencies, maintainability

- [ ] 4.6.1 Audit CMake presets/targets/install/package paths for configuration drift and clean-checkout reproducibility.
- [ ] 4.6.2 Review dependency inventory and vendored SQLite version/policy for known actionable issues; do not edit vendor code without a concrete reason.
- [ ] 4.6.3 Remove dead/duplicate/obsolete runtime code only when call/reference analysis proves it is unused and tests preserve behavior.
- [ ] 4.6.4 Reconcile README/support/release docs to actual current behavior and gates.

### Phase 4 finding rule

For every newly confirmed material finding, record:

- severity;
- exact path/function;
- reproducer/missing invariant;
- root cause where known;
- user/operational impact;
- dependency;
- remediation;
- regression test;
- acceptance/exit criteria.

Open a bounded OpenSpec before invasive implementation. P0/P1 fixes take precedence over continuing this checklist.

## 5. Regression hunting and completion convergence

- [ ] 5.1 Re-run focused regressions after every implementation wave and repair introduced failures immediately.
- [ ] 5.2 Run stress/repeated execution for previously flaky or concurrency-sensitive suites.
- [ ] 5.3 Inspect test value: add missing failure-path tests for material defects; remove/replace only tests proven obsolete or misleading.
- [ ] 5.4 Run performance sanity after correctness changes; reject material regressions unless explicitly justified by a stronger product requirement.
- [ ] 5.5 Re-enumerate active-local OpenSpec tasks. Zero is acceptable only if task ledgers still contain every required requirement.
- [ ] 5.6 Perform a final diff review for accidental debug code, secrets, temporary bypasses, commented assertions, test weakening, ignored artifacts, and unrelated churn.

## 6. Final release-certification gate

On one committed candidate SHA, run at minimum:

- [ ] 6.1 clean dependency/toolchain discovery and clean configure from scratch;
- [ ] 6.2 Debug build + **full CTest**;
- [ ] 6.3 release-test build + **full CTest**;
- [ ] 6.4 Analyze warning-clean;
- [ ] 6.5 authoritative ASAN/fuzz/lifecycle sanitizer slice;
- [ ] 6.6 strict OpenSpec validation;
- [ ] 6.7 source marker + diagnostic policy;
- [ ] 6.8 documentation truth + source provenance;
- [ ] 6.9 project-status + provider/result-composition/UIA-selection fixtures;
- [ ] 6.10 repository verifier status/doctor/build gates;
- [ ] 6.11 exact committed-tree qualification;
- [ ] 6.12 unsigned release-candidate packaging/checksums/provenance;
- [ ] 6.13 verify release evidence retention paths are populated;
- [ ] 6.14 representative large-index/search performance sanity;
- [ ] 6.15 major user-journey/UIA smoke available on the current host;
- [ ] 6.16 release-readiness report/composition with external blockers classified, never fabricated;
- [ ] 6.17 `git diff --check`, clean worktree, no accidental secrets/debug outputs;
- [ ] 6.18 push without force, confirm local HEAD == origin/main;
- [ ] 6.19 inspect hosted CI for the **exact final pushed SHA** and record run/job results;
- [ ] 6.20 if a post-certification commit changes code, build/test/CI truth, lifecycle, release scripts, or task authority, rerun the affected certification rather than carrying forward stale evidence.

## 7. Final handoff and stop condition

- [ ] 7.1 Write final WORKSTATE with start/end timestamps, starting/final SHA, branch/parity, changes opened/completed, findings, files/subsystems changed, exact commands/test counts, benchmark data, hosted run IDs, and external blockers.
- [ ] 7.2 Synchronize `progress.md`, lifecycle, active/queued OpenSpecs, audit, agent prompts, and release-readiness state.
- [ ] 7.3 State one of:
  - **locally complete; external certification pending**, with explicit external prerequisites; or
  - **not locally complete**, with exactly one highest-value execution-ready next frontier.
- [ ] 7.4 Stop if a fresh audit finds no evidence-backed material local work. Do not create cosmetic/speculative tasks to fill remaining wall-clock budget.

## Suggested autonomous-session steering

This is a priority guide, not a quota:

- first 2–4 hours: close the P1 rule-pruning dependency and obtain exact-SHA evidence;
- next 1–2 hours: truth-surface + CI/release/sanitizer hardening;
- next 2–3 hours: fresh correctness/security/concurrency audit and bounded fixes;
- next 1–2 hours: performance/UI/compatibility regression review;
- final 2 hours: complete validation, exact-tree qualification, hosted CI, state synchronization.

A confirmed P0/P1 may consume more time. Compress lower-priority review rather than abandoning the blocker.
