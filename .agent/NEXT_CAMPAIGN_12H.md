# FASTFILES — 12-HOUR COMPLETION-ORIENTED AUTONOMOUS CAMPAIGN

Status: ACTIVE
Planned-From: `57060c74e1d2416c8166201bbfd1834729ffd2da`
Planned-At: `2026-08-27T21:54:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Current-OpenSpec: `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`
Audit-Evidence: `docs/agent/DEEP_AUDIT_2026-08-27_FASTFILES.md`
Exact-SHA-CI: run `33033342573` = **FAIL**

## 0. Mission

Use the next autonomous engineering window to drive FastFiles as close as evidence permits to **full locally actionable project completion**, not merely to finish one optimization.

The preferred terminal state is:

- no active-local OpenSpec work remains;
- all confirmed locally reproducible correctness, durability, security, concurrency/lifetime, and release-gate defects are fixed;
- current product behavior is covered by deterministic tests at the highest-risk seams;
- Debug and release-test full CTest are green;
- Analyze is warning-clean;
- required ASAN slices are green;
- strict OpenSpec, source/diagnostic/doc/provenance policy, project-status, verifier, and exact committed-tree qualification are green;
- unsigned release-candidate qualification is green;
- hosted CI is green for the exact final pushed SHA;
- lifecycle, task ledgers, WORKSTATE, progress, and execution prompts match reality;
- only explicit external-evidence-only boundaries remain external.

Do not claim production release certification without the external evidence the repository requires.

## 1. Wall-clock contract

At executor start, record `campaign_start_utc` from a real wall clock and `campaign_deadline_utc = campaign_start_utc + 12 hours` in durable workstate. Treat twelve hours as an upper engineering budget and continuity contract, not permission to sleep, spin, or create cosmetic churn.

Continue through sequential evidence-backed OpenSpec campaigns until either:

1. the deadline is reached; or
2. a genuine **full local completion** audit proves there is no remaining evidence-backed locally actionable frontier.

If full local completion appears early, run a fresh repository-wide adversarial audit and assurance-gap pass before declaring that state. Never manufacture changes solely to consume time.

## 2. Safety and architecture invariants

Preserve all stricter repository rules, especially:

- Windows-native C++20 assumptions and the three-process privilege split;
- privileged `FastFilesIndexSvc`, unprivileged `FastFilesEngine`, and UI separation;
- the closed named-pipe command surfaces and least-privilege handles/ACLs;
- Authenticode/pin fail-closed behavior and no unsigned production bypass;
- degraded mode;
- zero-IPC keystroke search and snapshot/read-plane contracts;
- deterministic query semantics, ordering, result caps, cancellation, and generation/epoch behavior;
- long-running matching/path work outside `IndexPipeline::mutex_`;
- real storage lifetime ownership for any borrowed view;
- projection memory/eviction bounds and durable SQLite semantics;
- no vendored SQLite edits unless a separately justified dependency campaign exists;
- no force-push, destructive cleaning, or unrelated user-work loss.

External signing, privileged/clean-host/provider, and physical-display/UIA requirements remain external unless the actual environment is available. Never fake evidence.

## 3. Mandatory intake

Before edits:

1. fetch/prune and reconcile `main`, `origin/main`, HEAD, worktree/index/untracked state, ahead/behind, and recent commits;
2. read `AGENTS.md`, `AUTONOMOUS.md`, `.agent/PLANNER_HANDOFF.md`, this file, `.agent/EXECUTION_PROMPT.md`, `CODE_INDEX.md`, `docs/agent/WORKSTATE.md`, `progress.md`, `openspec/config.yaml`, `openspec/lifecycle.json`, and the active OpenSpec;
3. run the repository-native autonomous intake;
4. enumerate the current tracked tree and compare blob SHAs against the audit baseline; re-audit every changed path before trusting this plan;
5. inspect exact current hosted CI rather than assuming planner-time status;
6. preserve one active implementation OpenSpec at a time unless repository policy explicitly changes.

If current HEAD differs from Planned-From, current source and current repository contracts win. Update evidence and continue; do not blindly patch stale line numbers.

## 4. Wave 0 — repair the current false-red/false-green control plane

This is the first task because the current exact SHA cannot qualify.

### 4.1 Project-status lifecycle test

Hosted run `33033342573` failed Debug, release-test, and hygiene because `verify/status/tests/Test-ProjectStatus.ps1` contains a live-tree assertion that assumes all active local work belongs to the completed `openspec-lifecycle-evidence-integrity-and-semantic-convergence` campaign.

Required repair:

- derive the expected active-local set/count from lifecycle state and the generated frontier;
- do not hard-code the current indexed-search campaign name as the replacement;
- add a hermetic fixture proving that activating an arbitrary future campaign does not require changing the test;
- preserve fail-closed lifecycle/evidence validation;
- prove historical, superseded, external, mixed, and active-local buckets remain distinct.

### 4.2 Task-ledger truth

The active OpenSpec had task 7.3 checked even though its note said full Debug CTest was pending; exact-SHA CI later proved the full suite failed. Keep the planner correction that reopens it. Reconcile any other checkbox/evidence contradiction before completion.

### 4.3 Search timing attribution

In `SearchIndexedReadPlane`, `viewMatchingMs` currently measures the entire per-row loop, including survivor construction and `SearchAccumulator::Add`, while `survivorMaterializationMs` separately measures the survivor path. Those metrics overlap.

Make the timing contract explicit and non-overlapping, or rename/redefine fields so consumers cannot interpret them as a partition. Add coverage. Do not use contaminated attribution to justify the optimization.

## 5. Wave 1 — truthfully close the active indexed-search OpenSpec

Resume `.agent/EXECUTION_PROMPT.md` and the active change only after Wave 0.

Mandatory remaining gates include:

- task 2.2 lifetime-hazard coverage unless the design is revised with evidence;
- durable paired 1M benchmark JSON with exact commands/build/machine metadata;
- candidate/name materialization, rejected-row, lock wait/hold, fallback/restart, and transient-memory evidence;
- fixed non-overlapping matcher/materialization attribution;
- parity across selective/no-match/broad/complex/Unicode/churn/cancellation/cold-store paths;
- strict OpenSpec and all repository policy gates;
- full Debug/release-test CTest;
- Analyze and required ASAN;
- exact committed-tree qualification;
- exact pushed-SHA hosted CI inspection.

### Keep/revert gate is binding

Current committed evidence reports materially neutral wall time. Current `design.md` says the final selected implementation must show a repeatable material improvement on a representative resident selective workload (about <=5% is noise unless repeated evidence establishes otherwise) or the invasive optimization is reverted.

Do not silently reinterpret structural allocation reduction as satisfying a latency threshold written before implementation. Resolve the design/evidence conflict explicitly. Prefer preserving the existing acceptance threshold unless there is a legitimate, documented product-contract reason to amend it. A clean no-change/revert outcome is better than a post-hoc success definition.

When this campaign is truthfully COMPLETE/no-change, commit/push, confirm exact-SHA CI, update lifecycle/docs, then continue to Wave 2 while budget remains.

## 6. Wave 2 — OpenSpec: rule-pruning durability and reconciliation failure closure

This is the highest-value current post-search correctness frontier unless a more severe live defect appears.

Create a **new bounded OpenSpec** only after the indexed-search change is closed. Suggested name:

`rule-pruning-durability-and-reconciliation-failure-closure`

### Confirmed source seam

Current `IndexPipeline.cpp` has multiple secondary durable removals whose failure is not propagated equivalently to the primary write:

- rebuild/open rule pruning: a failed secondary `Store::ApplyBatch(ruleExcluded)` simply skips projection removals and continues;
- `ApplyMftBatchChanges`: primary ApplyBatch failure returns false, but rule-pruning ApplyBatch failure can still reach `return true`;
- `ApplyUsnBatchChanges`: same asymmetry;
- reconciliation: failed removal does not remove projection rows, but `reconciliationSeen_` is erased anyway, potentially discarding the current repair obligation.

`Store::ApplyBatch` itself documents transactional rollback and caller retry on failure. Determine the intended higher-level contract from current specs/callers before changing APIs.

### Required OpenSpec work

1. Map every writer mutation and secondary cleanup path: MFT, USN, rebuild/open, reconciliation, rules changes, forget/eviction/reload, restart.
2. Introduce deterministic writer fault injection at named transaction boundaries with production defaults unchanged. Avoid flaky filesystem/SQLite-lock tricks if an injectable seam can prove the contract.
3. Reproduce secondary-write failure and observe:
   - return status;
   - durable SQLite state;
   - resident projection state;
   - search, snapshot/browse, and aggregate visibility;
   - query-visible epoch/publication behavior;
   - reconciliation obligation retention;
   - restart and next-reconciliation behavior.
4. Decide and specify atomic/fail-closed vs explicit eventual-recovery semantics. Do not assume.
5. Make MFT/USN/rebuild/reconcile behavior consistent with the chosen contract.
6. Ensure failure cannot be reported as successful while leaving an untracked repair obligation.
7. Add regression tests for first failure, retry/recovery, repeated failure, cancellation/shutdown interaction, and restart.
8. Run the full validation ladder and exact-SHA CI before completing the change.

Correctness/data integrity outranks performance. If this seam confirms a user-visible stale/excluded-row state, finish it before opening lower-priority campaigns.

## 7. Wave 3 — fresh repository-wide completion audit

After Wave 2, re-enumerate the live tracked tree. Old audit findings are leads only; reproduce them on current source.

Audit in this order:

1. **Correctness/data integrity:** durable/store/projection divergence, mutation error propagation, stale publication, restart/rebuild equivalence, file operations/conflict handling.
2. **Security/privilege/IPC:** command-surface closure, peer authentication, frame bounds, opaque-handle scope, SCM rights, DLL loading, write deadlines, content-forwarding restrictions.
3. **Concurrency/lifetime:** shutdown races, cancellation, worker ownership, snapshot generations, UI/engine reconnect, Store writer/read-session separation.
4. **Resource bounds/performance:** projection/name memory accounting, compaction/eviction, pathological names, large result sets, lock contention, cold-stream behavior.
5. **UI state machines:** degraded/indexed transitions, unavailable volumes, queued/cancelled search, dual-pane selection/history/address-bar, preview/properties, file-operation errors, UIA identity/event consistency.
6. **Verification:** source paths not covered by CTest/Analyze/ASAN/exact-tree/hosted CI; false-green or stale hard-coded truth assumptions.
7. **Maintainability/docs:** only after the above; avoid cosmetic rewrites with no user/reliability benefit.

For every finding: record severity, exact path/function, reproducer or missing invariant, affected contract, and why an existing test fails to catch it. Open a bounded OpenSpec only for a confirmed material frontier.

## 8. Wave 4 — completion convergence

Before the 12-hour boundary, or whenever no stronger frontier remains:

- resolve every introduced Critical/High regression;
- finish all active-local OpenSpec tasks or leave a truthful active continuation if deadline stops the run;
- run strict OpenSpec;
- run Debug configure/build + full CTest;
- run release-test configure/build + full CTest;
- run Analyze warning-clean;
- run repository-required ASAN;
- run source/diagnostic/doc/provenance/status/provider/verifier gates;
- run exact committed-tree qualification;
- run unsigned release-candidate qualification;
- commit coherent waves and push without force;
- inspect hosted CI for the exact pushed SHA;
- regenerate/reconcile lifecycle, WORKSTATE, progress, and current prompts;
- prove local/remote parity.

### Full local completion gate

Do not write “100% complete”, “production ready”, or equivalent unless all locally enforceable gates above are green and a fresh audit found no remaining evidence-backed local frontier.

If external-evidence-only entries remain, say precisely: **locally complete / external certification pending**. Production signer, clean-host, elevated provider/privileged evidence, and physical-display/UIA evidence cannot be manufactured by local code changes.

## 9. Time steering

Suggested allocation, adjustable to evidence:

- 0.0–1.5h: Wave 0 control-plane + timing-contract repairs and focused tests.
- 1.5–4.0h: close/revert indexed-search campaign with durable benchmarks and full qualification.
- 4.0–7.5h: rule-pruning OpenSpec, deterministic writer fault injection, implementation, focused validation.
- 7.5–10.0h: fresh cross-subsystem completion audit + next bounded high-value repair if confirmed.
- 10.0–12.0h: full convergence, exact-tree/hosted validation, state reconciliation, final handoff.

Never abandon a confirmed Critical/High defect because its nominal time box expired. Compress lower-priority work instead.

## 10. Git and evidence discipline

- preserve unrelated work;
- no force push;
- checkpoint coherent implementation/evidence waves;
- fetch/prune before final pushes;
- bind claims to exact SHAs and exact run IDs;
- no “passed” claim from an unrun/skipped gate;
- no external PASS without actual external evidence;
- keep benchmark raw data only where repository policy expects it;
- task checkboxes change only from observed evidence;
- if CI fails, root-cause the failure before rerunning blindly.

Commit messages should be full-session engineering reports: root cause, implementation, tests, before/after measurements, exact CI/qualification evidence, remaining blockers, and next frontier.

## 11. Required final handoff

Write a durable final handoff with:

- campaign start/deadline/end timestamps;
- starting and final SHA, branch, origin parity;
- OpenSpecs opened/completed/reverted/left active;
- all confirmed findings and severity;
- files/subsystems changed and why;
- exact commands and test counts;
- benchmark data and build/machine context;
- exact hosted run IDs;
- external-only blockers;
- zero/nonzero active-local task count;
- whether full local completion was actually achieved;
- if not, one execution-ready next frontier.

At the deadline, prefer a safe pushed checkpoint and truthful continuation over a rushed unsafe “complete” label.
