# NEXT CAMPAIGN — FASTFILES

Status: **ACTIVE**
Planned-From: `90d012ebfbcd7530f5b397528dc768de34a7396f`
Planned-At: `2026-08-25T17:34:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **Verification-Orchestration Convergence + Deterministic Evidence Hardening**

## Mission

Take the actual current `main` and eliminate the remaining locally actionable ambiguity in FastFiles' verification system: the exported/live `windows-ui-automation-validation` capability can produce a truthful green result (`23 PASS / 6 SKIP / 0 FAIL` in the latest recorded campaign evidence), while the supported `pwsh ./verify/verify.ps1 run ...` orchestration path can still return an opaque `scenario-failed` result for a mixed run.

Treat that discrepancy as a verification-platform defect until proven otherwise. Determine exactly whether the divergence is caused by capability orchestration, provider lifecycle, process ownership/cleanup, scenario ordering, stale UIA fragments, foreground ownership, result-envelope aggregation, run-tree/report generation, or an actual product state transition. Fix the locally actionable root cause(s), add durable deterministic coverage, and make the supported verifier path tell the same evidence-backed truth as the underlying capability.

This is a hardening campaign, not a speculative feature campaign. The preceding ship-ready UI/UX convergence, active-path resilience, search-availability, indexed-read-plane, and runtime-determinism work must be preserved. Do not manufacture unrelated product churn merely to make this campaign large.

## Why this is the next campaign

Current repository evidence says the previous local product-hardening frontier is complete:

- `main` contains the ship-ready UI/UX hardening implementation and pushed handoff.
- Debug and release-test CTest were recorded green at `81/81`; Analyze was warning-clean; ASAN protocol/IPC was green; strict OpenSpec, source-marker, diagnostic-policy, doc-drift, verifier build/doctor, and diff hygiene were green.
- The current UIA/provider/runtime work repaired stale fragment discovery, Pane.0/Pane.1 reconstruction, exact process ownership, Search.Results reacquisition, and deterministic mock tree dumps.
- PR #6 is stale, draft, non-mergeable, and based on an old baseline. Current `main` is far ahead and already re-evaluated/reimplemented the valid ideas. It is evidence only, not an implementation source.
- No open GitHub issues currently define a new implementation feature frontier.
- Remaining production signing, privileged MFT/USN, clean-host, guest/VM, physical-display, and externally owned foreground evidence are environment/release prerequisites and must not be converted into fake local engineering work.

One important local discrepancy remains: the direct/exported UIA capability result and the supported verifier wrapper can disagree. Because every future autonomous campaign relies on this verifier to distinguish PASS / FAIL / SKIPPED / BLOCKED states, this discrepancy is now the highest-leverage local hardening target.

## Required reconnaissance before edits

Do not assume the planner's snapshot is still current. Begin by:

1. `git fetch --prune origin`.
2. Record current branch, local HEAD, `origin/main`, ahead/behind counts, worktree status, and recent history.
3. Start from current `main`; fast-forward safely if needed. Never reset away valid work and never force-push.
4. Re-read, at minimum:
   - `AGENTS.md`
   - `.agent/PLANNER_HANDOFF.md`
   - this `.agent/EXECUTION_PROMPT.md`
   - `progress.md`
   - `docs/agent/WORKSTATE.md`
   - `README.md`
   - `CODE_INDEX.md`
   - `.github/workflows/ci.yml`
   - `verify/README.md`
   - `verify/verify.ps1`
   - `verify/core/CapabilityRunner.psm1`
   - `verify/core/RunTree.psm1`
   - `verify/core/Reporting.psm1`
   - `verify/core/Providers.psm1`
   - `verify/providers/local/Local.psm1`
   - `verify/capabilities/windows-ui-automation-validation/capability.json`
   - `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`
   - `verify/capabilities/windows-ui-automation-validation/UiAutomationWin32.psm1`
   - `verify/uia-driver/**` relevant to process/root reacquisition, input, mock trees, and state
   - `openspec/changes/interactive-ui-runtime-determinism/**`
   - `openspec/changes/ship-ready-ui-ux-hardening-convergence/**`
   - the recent commits from runtime determinism through ship-ready UI/UX convergence.
5. Inspect current open PR/issue state. PR #6 must be treated as stale audit evidence only; do not merge it wholesale or blindly cherry-pick it.
6. Inspect the actual current implementations affected by recent UI/navigation/search/storage/runtime-determinism changes, not only the changed files. Trace their callers, state ownership, lifecycle interactions, tests, and verification surfaces across the application.

## Fresh campaign ledger

Create a new OpenSpec change for this campaign, with a name equivalent to:

`verification-orchestration-convergence`

Include at least:

- `.openspec.yaml`
- `proposal.md`
- `design.md`
- `tasks.md`
- appropriate spec deltas/new specs
- `evidence/baseline.md`
- `evidence/implementation-and-validation.md`

The evidence ledger must distinguish:

1. reproduced local defects;
2. harness/orchestration defects;
3. actual product defects discovered during reproduction;
4. stale/superseded historical assumptions;
5. environment-only foreground/Explorer/elevation limitations;
6. production-signing/privileged/clean-host external gates.

Do not call an item fixed merely because an isolated scenario passes. Record the exact execution path that passed.

## Workstream 1 — Reproduce and classify the wrapper divergence

Build a deterministic reproduction matrix for the supported verifier path.

At minimum compare:

- direct invocation of the UIA capability entry point when supported by current tooling;
- `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation`;
- the exact mixed/default capability set that previously produced the opaque `scenario-failed` result;
- isolated scenario selection via `-CapabilityOption UiaScenario=...` where supported;
- repeated clean-lifecycle runs;
- scenario-order permutations that are safe to automate;
- debug vs debug-diagnostic executable selection;
- fresh-shell vs same-shell runs where module/global state may matter.

For every divergence capture:

- exit code;
- capability envelope status/reason/summary;
- subresult statuses and reason codes;
- provider preflight/provision/activate/collect/cleanup state;
- exact product/engine PIDs, paths, start times, session IDs, and owned-pipe state;
- selected executable path/configuration;
- run-tree/report paths;
- scenario ordering;
- foreground owner when input is required;
- whether the failure occurred before a product assertion, during an assertion, during cleanup, or during report generation.

Do not reduce this to "retry until green". A retry is diagnostic data, not acceptance evidence.

## Workstream 2 — Run-tree and result-envelope truthfulness

Audit the complete result path from capability availability through `Invoke-Capability`, capability return object, `New-ResultEnvelope`, persistence, run index generation, Markdown/JSON/HTML/JUnit reporting, and `Get-VerbExitCode`.

Required properties:

- A capability with zero FAIL subresults must not become an unexplained FAIL later in orchestration.
- A real FAIL must never be softened into PASS/SKIPPED by aggregation.
- `SKIPPED` / environment-unavailable rows remain explicit and reason-coded.
- capability exceptions become a structured harness error with enough diagnostic context to identify the failing phase, not a generic `scenario-failed` blob.
- envelope status, subresults, report summaries, CLI exit code, and persisted run-tree result agree.
- report generation/fidelity checks cannot silently overwrite or obscure the original capability failure reason.
- partial capability filters remain correctly marked as partial and do not contaminate persistent baselines.

Add focused noninteractive PowerShell tests for the result/aggregation logic. Prefer fixture envelopes and disposable run trees so CI can exercise these invariants without a real desktop.

## Workstream 3 — Provider lifecycle and cleanup determinism

Audit the local provider lifecycle around the UIA capability and any other capability that launches repository-built processes.

Verify and harden as needed:

- preflight/provision/activate/collectLogs/cleanup phase ordering;
- cleanup execution on success, capability FAIL, exception, timeout, and report-generation failure;
- exact repository-build process identity rather than process-name-only cleanup;
- PID reuse defense;
- build-path validation;
- process start-time tolerance;
- engine-pipe disappearance checks;
- no accidental termination of installed FastFiles or unrelated same-name processes;
- no stale provider/module/global state leaking from one capability to the next;
- cleanup failure remains first-class evidence instead of being collapsed into the prior scenario result.

If the mixed wrapper failure is caused by lifecycle contamination, add a deterministic headless regression that reproduces the contamination without depending on foreground UI input.

## Workstream 4 — UIA scenario isolation and order independence

Audit the current UIA capability's scenario coordinator, not merely individual scenario helpers.

Preserve the recent fixes for:

- direct-child live-root reacquisition;
- `Pane.0` / `Pane.1` identity;
- fragment-generation lifetime and stale-element rejection;
- Search.Results reacquisition;
- exact owned-process cleanup;
- semantic command-palette dispatch;
- RuntimeId/AutomationId stability;
- pane-local selection semantics;
- deterministic mock-tree bounds without live wall-clock expiry.

Then prove:

- interaction-heavy scenarios begin from a defined fresh state;
- one scenario cannot leave settings/dialog/palette/search/storage/dual-pane state that changes the next scenario's classification;
- scenario filtering executes only the requested scenario plus explicit prerequisites;
- scenario result ordering is deterministic;
- expected foreground unavailability is emitted as `SKIPPED` before input-dependent assertions run;
- a product assertion failure remains FAIL and is not downgraded to environment SKIP after the fact;
- the complete mixed capability converges to the same semantic verdict as the set of isolated scenarios, modulo explicitly modeled environment skips.

If an actual product state/lifecycle bug is discovered, fix it narrowly and add product-level headless regression coverage. Do not broadly rewrite UI architecture merely because the verifier exposed a race.

## Workstream 5 — Broad impact audit of the recently hardened product seams

Because the verifier is the evidence layer for recent product changes, audit how the current verification semantics cover the entire affected system rather than checking only the newest files.

Trace and verify the interactions among:

- NavigationWorkspace / NavigationChrome / tabs / breadcrumbs / exact editable address identity;
- Home / recent folders / sidebar / bookmarks;
- SearchPanel async result gating and Search UIA provider lifetime;
- ColumnView / dual-pane transitions / ColumnViewUiaProvider fragment replacement;
- Storage Analysis mode state, Back/Up, keyboard radio topology, degraded/unavailable status refresh, and treemap activation;
- theme / High Contrast / DPI repaint paths;
- EngineClient reconnect/generation updates that can trigger UI replacement;
- process launch/engine ownership and shutdown;
- file-operation/navigation identities that must continue using canonical paths rather than display-normalized paths.

The goal is to ensure a green verifier still represents the complete behavior of the recent hardening work. Add missing durable tests only where the audit finds a genuine coverage hole or integration risk.

## Workstream 6 — CI-safe verification of the verifier

Hosted Windows CI cannot be trusted to own an interactive foreground desktop. Do not turn UIA-interactive flows into flaky CI requirements.

Instead add or improve noninteractive CI coverage for:

- capability registry/load contracts;
- envelope aggregation;
- run-tree persistence;
- report fidelity;
- provider lifecycle cleanup using fixtures/mocks;
- UIA driver recorded-tree/mock semantics;
- scenario-selection/orchestration logic that can run without a live desktop;
- exact reason-code preservation.

Keep true interactive UIA rows availability-gated. CI should prove orchestration correctness without pretending to prove physical foreground interaction.

Inspect `.github/workflows/ci.yml` after implementation and add only the minimum stable job/step coverage needed for these new deterministic tests.

## Workstream 7 — Failure taxonomy and diagnostics

Replace ambiguous failure surfaces with structured, durable classifications where evidence supports them.

At minimum distinguish:

- `product-assertion-failed`;
- `scenario-precondition-failed`;
- `interactive-foreground-unavailable`;
- `explorer-drag-source-unavailable`;
- `provider-lifecycle-failed`;
- `owned-process-cleanup-failed`;
- `engine-pipe-cleanup-failed`;
- `uia-element-stale-or-replaced`;
- `scenario-timeout` with the stage recorded;
- `capability-exception` / `harness-error`;
- report/run-tree fidelity failure.

Do not rename failures cosmetically. Every new reason code must correspond to a concrete decision point and have focused test coverage.

## Workstream 8 — Validation

Run focused tests first, then the broad gates. Fix all locally actionable failures introduced or exposed by this campaign.

Required final local validation, when the environment supports it:

1. Configure/build Debug.
2. Focused verifier/core/provider/UIA-driver tests.
3. Full Debug CTest.
4. Configure/build release-test and full release-test CTest.
5. Analyze configure/build with zero new warnings.
6. Applicable ASAN protocol/IPC fuzz/parser targets; do not broaden ASAN scope merely for optics.
7. `pwsh ./verify/verify.ps1 build`.
8. `pwsh ./verify/verify.ps1 doctor`.
9. New verifier orchestration self-tests.
10. `windows-ui-automation-validation` through the supported `verify.ps1 run` path.
11. Isolated UIA scenario runs sufficient to prove mixed-vs-isolated semantic convergence.
12. Source-marker policy.
13. Diagnostic-policy gate.
14. Doc-drift tests.
15. Strict OpenSpec validation for all changes.
16. `git diff --check`.
17. Inspect hosted CI for the final pushed commit and fix any local-actionable CI failure.

For live UIA evidence:

- preserve `interactive-foreground-unavailable` and Explorer-source limitations as truthful skips;
- never send input after a failed foreground precondition;
- never claim 20/20 or similar repeated-interaction success unless it was actually observed;
- never convert an opaque wrapper failure to PASS merely because isolated scenarios are green;
- the campaign is complete only when the supported wrapper's final semantic verdict is explained and deterministic.

## Preserved behavior / non-negotiable constraints

Do not regress or weaken:

- the three-process privileged/unprivileged architecture;
- the closed command protocol and 1 MiB pre-allocation frame bound;
- symmetric pinned Authenticode peer authentication;
- fail-closed production release configuration;
- service ACL/SCM restrictions;
- no-forwarded-file-content invariant;
- DLL search hardening;
- immutable snapshot publication semantics;
- degraded mode as a first-class supported path;
- exact canonical navigation/file-operation identity;
- display-only long-path/8.3 normalization boundaries;
- Search cancellation/supersession semantics;
- UIA fragment lifetime and stale-provider rejection;
- High Contrast/native accessibility behavior;
- no host-wide process killing or trust-store mutation during ordinary development.

Production signing, trust-store integration, privileged MFT/USN qualification, clean-host installation, guest/VM providers, and physical display matrices are out of scope unless the required authorized environment is already present. Record them as external prerequisites, not local defects.

## PR #6 disposition

PR #6 (`stabilization/ship-ready-2026-08-23`) remains stale audit evidence. Current `main` is substantially ahead and has already reconciled its valid ideas. Do not merge or cherry-pick it as part of this campaign.

If authenticated GitHub administration is available at final handoff and current-main evidence still proves PR #6 fully superseded, it may be closed with a concise explanation pointing to the mainline hardening commits. This is administrative cleanup, not an acceptance dependency for local engineering.

## Acceptance criteria

This campaign is complete only when all of the following are true:

- the original mixed-wrapper divergence is reproduced or conclusively invalidated on current `main` with evidence;
- the root cause is classified precisely;
- every locally actionable root cause is fixed;
- the supported `verify.ps1 run` path yields a deterministic, structured semantic result consistent with the underlying capability evidence;
- external foreground/elevation/provider limitations remain explicit reason-coded skips/blockers;
- no generic `scenario-failed` remains where a more precise locally knowable reason exists;
- scenario ordering/lifecycle cannot contaminate later scenarios without being detected;
- run-tree, reports, envelope, subresults, and CLI exit code agree;
- deterministic headless/fixture tests protect orchestration and cleanup logic in CI;
- the recent Navigation/Search/Storage/dual-pane/runtime hardening remains behaviorally intact;
- required Debug/release-test/Analyze/ASAN/verifier/policy/OpenSpec/hygiene gates are green or any unavailable external gate is truthfully documented;
- campaign OpenSpec evidence, `progress.md`, `docs/agent/WORKSTATE.md`, and any genuinely affected README/CODE_INDEX/verify docs are updated to match observed reality.

## Completion gate / stop rule

Continue autonomously until there is no meaningful implementation-ready work left in this campaign.

Do not stop merely because one milestone or one scenario passes. Re-run the full supported wrapper after fixes and inspect the broader impact on the verification system and the recently hardened product seams.

If the investigation proves the remaining divergence is entirely an external interactive-desktop limitation and no local verifier/product defect remains, do not manufacture code churn. Instead add only the missing deterministic classification/coverage/documentation needed to make that fact machine-readable and reproducible, then stop.

## Git requirements

- Work from current `main` after fetch/prune and reconciliation.
- Preserve unrelated valid work.
- Never force-push.
- Keep generated `verify/runs/`, `verify/baselines/`, signing material, and other ignored runtime evidence out of Git.
- Commit implementation in coherent checkpoints when useful.
- Final commit message must be a detailed session handoff: baseline, root cause, implementation, tests, remaining external boundaries, and final status.
- Fetch/prune again before final push and reconcile safely if `origin/main` moved.
- Push all completed work to `origin/main`.
- Verify local HEAD equals `origin/main` and the worktree is clean.
- Inspect hosted CI for the pushed tip and repair local-actionable failures before declaring completion.

## Final report

At completion, update durable repository state and report:

- start and final SHAs;
- exact reproduced divergence and root cause classification;
- files/subsystems changed;
- product behavior preserved;
- verifier/orchestration behavior changed;
- focused and full validation results;
- live UIA results including all truthful skips;
- hosted CI state;
- PR #6 administrative disposition if changed;
- remaining external production/privileged/environment prerequisites;
- whether any meaningful local implementation frontier remains.

Do not output a new planner prompt at the end. Mark this prompt non-active/completed in durable state or otherwise make it clear to the next `/goal continue` that the campaign has finished, then push the final handoff.
