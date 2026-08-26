# FASTFILES — 12-HOUR AUTONOMOUS NEXT-CAMPAIGN ORCHESTRATOR

Status: ACTIVE
Planned-From: 9cec4f06ca3dae652b05d13615cb8ef2f7e13ea2
Planned-At: 2026-08-26T22:57+08:00
Target-Branch: main
Primary-OpenSpec: `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/`
Audit-Evidence: `docs/agent/DEEP_AUDIT_2026-08-26_FASTFILES.md`

## 0. Read this first

This file is the repository-level executor contract for the next autonomous FastFiles run. It extends, but does not replace, repository rules in `AGENTS.md`, `AUTONOMOUS.md`, `.agent/PLANNER_HANDOFF.md`, `.agent/EXECUTION_PROMPT.md`, `openspec/config.yaml`, and `openspec/lifecycle.json`.

The existing active OpenSpec change, `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`, is Campaign 1. Execute it first. When that bounded change is completed or evidence shows that its invasive optimization should be rejected, **do not end the autonomous run merely because Campaign 1 is finished**. Re-audit the live tree, create the next bounded OpenSpec change, execute it, validate it, and repeat while the 12-hour campaign budget remains.

A disproven optimization is a successful evidence result, not a reason to force code and not a reason to end the 12-hour campaign.

## 1. Twelve-hour wall-clock contract

At executor start:

1. Record `campaign_start_utc` using an actual wall clock.
2. Set `campaign_deadline_utc = campaign_start_utc + 12 hours`.
3. Record both timestamps in the durable campaign log/workstate.
4. Continue productive, repository-relevant work until the deadline unless the user explicitly stops the run or the repository becomes genuinely inaccessible/corrupt in a way that prevents safe progress.

The 12 hours are a **productive engineering budget**, not permission for busywork. Do not sleep, spin, create cosmetic churn, manufacture findings, or keep bad code merely to consume time. If an implementation frontier closes early, pivot to deeper verification, fault injection, benchmark evidence, test-gap closure, current-source audit, documentation truth reconciliation, or the next evidence-backed OpenSpec.

External-only blockers such as production signing identities, unavailable privileged hardware/provider/guest environments, physical-display/UIA evidence, or unavailable clean-host infrastructure do **not** justify ending early. Respect repository policy: do not mutate trust stores, weaken security, fabricate external evidence, or chase signing. Instead, continue with locally actionable work and/or prepare deterministic harnesses/evidence collection for the blocked boundary.

## 2. Non-negotiable safety and architecture invariants

Preserve all stricter repository rules. In particular:

- Windows-only C++20 product assumptions remain intact.
- Preserve the three-process privilege split: `FastFilesIndexSvc` (privileged), `FastFilesEngine` (unprivileged), `FastFiles` UI.
- Preserve the two named-pipe seams and zero-IPC keystroke-search design.
- Do not broaden privileged commands, handles, SCM rights, forwarded data, DLL search policy, trust policy, or authentication surface.
- Never move long-running search matching under the engine mutation/state mutex to solve a pointer-lifetime problem.
- Never let a borrowed projection/name pointer outlive the storage generation that owns it.
- Preserve degraded-mode behavior and fail-closed publication/authentication semantics.
- Do not edit vendored SQLite except for a separately justified dependency-management campaign.
- Keep query semantics canonical across resident/cold, owned/view, accelerated/fallback, and restart/churn paths.
- Critical/High regressions introduced by the campaign must be fixed before delivery.

## 3. Mandatory intake and repository-wide reconciliation

Before product edits:

1. `git fetch --all --prune` and reconcile local `main`, `origin/main`, worktree/index/untracked state, ahead/behind, and recent commits.
2. Read `AGENTS.md`, `AUTONOMOUS.md`, `.agent/PLANNER_HANDOFF.md`, this file, `.agent/EXECUTION_PROMPT.md`, `CODE_INDEX.md`, `docs/agent/WORKSTATE.md`, `progress.md`, `openspec/config.yaml`, and `openspec/lifecycle.json`.
3. Run the repository-supported autonomous intake (`verify/intake.ps1 autonomous`) or the current canonical equivalent if the repository has changed.
4. Enumerate the **current tracked tree**, not only files named in old audits. Classify every tracked path by subsystem: build/config, protocol, IPC, setup/trust, service, engine, index store, search, UI, installer, tests/benchmarks, verification, OpenSpec/docs, generated/policy assets, and vendored dependencies.
5. Reconcile current implementation against current OpenSpec requirements and tests. Old reports are leads only; reproduce old findings against live code before acting.
6. Pin the exact starting SHA and hosted-CI state in the evidence log.

If the tree changed after this planner commit, current code and current repository rules win. Update the plan rather than blindly applying stale line-level assumptions.

## 4. Campaign 1 — execute the active indexed-search OpenSpec

Canonical sources:

- `.agent/EXECUTION_PROMPT.md`
- `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/proposal.md`
- `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/design.md`
- `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/tasks.md`
- its spec delta/baseline/audit evidence

### Required outcome

Measure and, only if justified, eliminate avoidable owned `Candidate`/`std::wstring` name materialization for resident projection rows rejected by canonical matching, while reducing mutation-lock pressure without changing search semantics or creating dangling views.

### Required proof before implementation

- Re-baseline current post-ASCII-fold performance.
- Map every invalidation event for `NamePool` views, projection entries/cursors/indices, `CompactStorage`, eviction/reload, generation changes, and query-visible/dependency epochs.
- Compare the candidate lifetime strategies in the active design; choose the least invasive design that is measurably useful.
- Add tests that prove the chosen lifetime model rather than relying on generation numbers as a substitute for pointer ownership.

### Keep/revert gate

Keep an invasive optimization only when paired evidence shows a repeatable material gain outside noise, with exact result/order/truncation parity and no material lock, memory, cancellation, churn, cold-store, or Unicode regression. If the premise fails, revert the invasive part, preserve useful tests/diagnostics where appropriate, record the no-change conclusion, complete the OpenSpec truthfully, and continue to Section 5.

## 5. Frontier loop — mandatory after Campaign 1 while budget remains

After each completed/no-change OpenSpec:

1. Run the focused validation for that change.
2. Reconcile tasks, evidence, lifecycle, `WORKSTATE.md`, `progress.md`, and relevant docs.
3. Commit a coherent checkpoint and push without force.
4. Inspect exact-SHA hosted CI when available.
5. Re-enumerate/re-audit the live tree and recent diff for the next highest-value local frontier.
6. Rank candidates by: correctness/security > durability/data integrity > concurrency/lifetime > performance/resource bounds > observability/testability > maintainability/UI polish.
7. Require a concrete reproducer, failing/absent invariant, measurable bottleneck, contract mismatch, or high-confidence test gap before source changes.
8. Create a **new bounded OpenSpec change** (proposal + design + tasks + spec delta where applicable + baseline/evidence) before implementation.
9. Keep only one implementation change active at a time unless repository OpenSpec policy explicitly supports otherwise.
10. Execute, validate, reconcile, commit/push, and repeat until `campaign_deadline_utc`.

Do not create a mega-spec covering unrelated fixes. The 12-hour run is a sequence of bounded, evidence-gated campaigns.

## 6. Highest-priority post-Campaign-1 investigation

The deep audit found a concrete seam that should be investigated first after Campaign 1 unless a more severe live failure appears.

### Candidate frontier: rule-pruning durability failure propagation

Current `IndexPipeline` ingestion flow persists the incoming MFT/USN batch, updates the projection, evaluates volume rules, then performs a second durable removal batch for newly excluded rows. The current source path appears to remove those rows from the projection only when the second `Store::ApplyBatch({}, ruleExcluded)` succeeds, but it does not propagate that second-batch failure to the caller; the ingestion method can continue and report success. Reconciliation/rebuild cleanup contains closely related second-mutation behavior.

This is **not pre-labeled as a bug**. First determine the intended contract from current OpenSpec, tests, recovery/reconciliation semantics, and callers. Then add deterministic fault injection around the second durable mutation.

Questions that must be answered:

- Can an excluded row remain durably present after the method reports success?
- Can it remain projection-visible to browse/snapshot/aggregate consumers even if search-time rule checks mask it?
- Does a later reconciliation deterministically repair the state, and is eventual repair an explicit contract?
- Should the operation fail closed, retry, defer publication, or record a recovery obligation when the pruning mutation fails?
- Are MFT, USN, rebuild, and reconciliation paths behaviorally consistent?
- Can a failed cleanup be lost because reconciliation bookkeeping is cleared anyway?

If the current contract requires atomic/fail-closed rule exclusion, create a dedicated OpenSpec such as `rule-pruning-durability-and-reconciliation-failure-closure` and repair it with fault-injection tests. If the current contract intentionally permits eventual reconciliation, document that explicitly and add tests proving bounded deterministic recovery; do not invent a stronger transactional model without evidence.

## 7. Secondary audit frontiers if the rule-pruning seam is not actionable

Investigate these in order of evidence, not as automatic implementation tasks:

### 7.1 NamePool/storage boundary hardening

If Campaign 1 changes storage ownership, re-check:

- `std::vector<char16_t>` view invalidation during growth and `Projection::CompactStorage` pool replacement;
- `size_t` to `uint32_t` narrowing for stored offsets/lengths under configured memory ceilings;
- whether `MemoryUsageBytes()` materially undercounts per-hash bucket/vector capacity and can weaken the projection memory ceiling;
- reserve/compaction/eviction determinism under large Unicode-heavy names.

Only change this if a reproducible bound/overflow/accounting problem exists or Campaign 1 requires a safer storage contract.

### 7.2 Mutation/read-plane fault matrix

Audit failure propagation and recovery across SQLite writer transactions, independent read sessions, projection mutation, snapshot publication, cold durable reads, generation restarts, cancellation, and shutdown. Prefer deterministic fault injection over speculative rewrites.

### 7.3 Security/IPC regression audit

Re-run current-source checks for closed command surfaces, opaque-handle scope, frame bounds, peer image/signature authentication, SCM rights, DLL hardening, write deadlines, and content-forwarding prohibitions. Old security audit findings must be reproduced on current source before re-opening them.

### 7.4 UI/engine state-machine coverage

Inspect whether user-visible modes (degraded/indexed, unavailable volume, reconnect/restart, queued/cancelled search, file-operation conflicts, preview/properties, dual-pane, history/address bar) have deterministic state transitions and tests. Prioritize correctness and stale-state bugs over cosmetic rewrites.

### 7.5 Verification and provenance gaps

Look for source paths that are not exercised by Debug/release-test/Analyze/ASAN slices, committed-tree verification, or exact-SHA hosted CI. Add harness coverage when it closes a real assurance gap.

## 8. Suggested time allocation — targets, not hard cutoffs

Use these as steering targets. Never abandon an unresolved Critical/High introduced regression merely because a phase window elapsed.

- Hour 0.0–0.75: intake, exact-SHA reconciliation, current tracked-tree inventory, baseline refresh.
- Hour 0.75–5.0: Campaign 1 lifetime proof, implementation/no-change decision, adversarial tests, benchmarks.
- Hour 5.0–6.0: Campaign 1 full validation, exact-tree commit/push/CI evidence, state reconciliation.
- Hour 6.0–7.0: fresh frontier audit and fault-injection design.
- Hour 7.0–10.0: Campaign 2 OpenSpec + implementation or evidence-backed no-change closure.
- Hour 10.0–11.25: next highest-value bounded hardening/test/benchmark frontier.
- Hour 11.25–12.0: final exact-tree validation, lifecycle/doc truth reconciliation, final push, remote parity, handoff.

If Campaign 1 legitimately consumes more time, compress later phases rather than skipping its correctness gates.

## 9. Validation ladder for every implementation campaign

Use the narrowest relevant tests first, then widen:

1. New/modified focused tests and reproducer.
2. Relevant search/indexstore/engine/protocol/IPC/setup/UI suites.
3. Strict OpenSpec validation.
4. Debug configure/build + full CTest.
5. release-test configure/build + full CTest.
6. Analyze configure/build warning-clean.
7. Repository-required ASAN protocol/IPC slice.
8. `verify/verify.ps1` and committed-tree/source/provenance policy gates as required by current repo instructions.
9. Commit, push without force, inspect exact pushed SHA and hosted CI.

Never report an external-evidence gate as passed unless the evidence actually exists.

## 10. Checkpoint and Git discipline

- Start from a reconciled current branch; do not overwrite unrelated user work.
- Commit logical, reviewable waves; do not wait until hour 12 for the first durable checkpoint.
- Before each finalizing push, fetch/prune and re-check divergence.
- No force push.
- Commit messages must summarize the implementation/evidence wave, tests, relevant measured before/after data, and remaining blockers/frontier.
- Keep generated artifacts and temporary benchmark output out of version control unless repository policy explicitly requires them.
- Update OpenSpec task checkboxes only from actual evidence.

## 11. Mandatory final handoff at the 12-hour boundary

At or immediately before `campaign_deadline_utc`, leave the repository in a safe, understandable state and push all intended commits. Write a final durable handoff containing:

- campaign start/deadline/end timestamps and productive duration;
- starting SHA, final SHA, branch, origin parity;
- every OpenSpec opened/completed/no-change/left active;
- files/subsystems changed and why;
- defects found and severity, including rejected hypotheses;
- exact test/build/verify commands and pass/fail counts;
- benchmark before/after data and machine/build configuration;
- hosted-CI runs for exact pushed SHAs;
- external blockers that remain external-only;
- any introduced/repaired Critical/High regressions;
- the single best next frontier, with evidence and a ready OpenSpec/prompt if work remains.

If work is mid-campaign at the deadline, do not rush an unsafe merge. Commit/push the safe completed checkpoint, mark remaining tasks truthfully, and leave an execution-ready continuation.

## 12. Definition of success

This 12-hour campaign succeeds when the executor has used the wall-clock budget for evidence-backed FastFiles engineering, not when it maximizes diff size. A valid result may contain both implementations and measured no-change decisions. The final tree must be safer or better understood than the starting tree, current documentation/OpenSpec state must match reality, and the next agent must be able to continue without reconstructing hidden context.
