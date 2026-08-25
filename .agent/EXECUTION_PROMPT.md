# NEXT CAMPAIGN — FASTFILES

Status: **COMPLETE**
Planned-From: `519a647a37a9fe517c7d744b99275569c73bbbb2`
Planned-At: `2026-08-25T19:28:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **OpenSpec Frontier Truthfulness + Historical Lifecycle Reconciliation**

## Mission

Make FastFiles' repository-status and autonomous-continuation machinery tell the truth about **what work is actually active now**.

The latest runtime-certification/verifier-convergence campaign has completed all locally actionable implementation work. Its remaining unchecked items are explicitly host-bound, interactive, privileged, signing, clean-host, or other external evidence gates. Do **not** manufacture another verifier/UI campaign simply because those evidence tasks are still unchecked.

The next locally actionable hardening problem is in the project-status/OpenSpec frontier model itself: `verify/core/ProjectStatus.psm1` currently scans every `openspec/changes/**/tasks.md`, treats every unchecked checkbox as current frontier work, and classifies tasks largely from task-text keywords. This can cause historical campaigns whose implementation and delivery were completed later to reappear as current `localOpen` work, while genuinely external gates must remain visible and blocked. The generated status is consumed by release-readiness reporting and by agents deciding what to do next, so stale frontier data can make future autonomous campaigns choose the wrong work.

Fix this at the lifecycle/state-model seam, reconcile historical ledgers only where evidence justifies it, add deterministic regression coverage, and leave FastFiles with one machine-readable, evidence-backed definition of active local work versus completed/historical/external work.

This is a repository-state hardening campaign. It is **not** permission to rewrite completed product architecture, revive stale PR #6, weaken security boundaries, or rerun host-bound live UIA loops under the already-recorded resource condition.

## Planner findings that must be independently revalidated

Treat these as starting evidence, not unquestionable truth. Re-read current `main` before edits.

1. Current planner baseline is `519a647a37a9fe517c7d744b99275569c73bbbb2` on `main`.
2. The immediately preceding runtime-certification campaign is marked complete in `.agent/EXECUTION_PROMPT.md` and `docs/agent/WORKSTATE.md`; current workstate says no local implementation action remains for that campaign.
3. `openspec/changes/runtime-certification-and-verifier-convergence/tasks.md` still contains legitimate unchecked items such as timing/repeatability/full-live-UIA evidence. Those are not automatically local implementation debt; current workstate explicitly records the host-resource/interactive boundary.
4. `Get-ProjectStatusOpenSpecFrontier` in `verify/core/ProjectStatus.psm1` recursively scans all `tasks.md` files under `openspec/changes` and collects unchecked `- [ ] N.N ...` lines without an explicit change lifecycle filter.
5. `Get-ProjectStatusTaskClassification` primarily infers category/status from regular-expression matches in task prose. A task with no recognized external keyword becomes low-confidence `local` work.
6. `New-ProjectStatus` then puts those inferred local unchecked tasks into `frontier.localOpen`.
7. Existing `verify/status/tests/Test-ProjectStatus.ps1` proves the text classifier and that the live parser can parse all ledgers, but it does not prove lifecycle-aware exclusion of completed/historical campaigns.
8. Historical examples require reconciliation, not blind checkbox editing:
   - `openspec/changes/real-user-dogfooding-release-engineering/tasks.md` still has 5.4 unchecked for final remote reconciliation/push/hosted-CI inspection, even though `main` contains many later pushed commits and hosted-CI evidence elsewhere.
   - `openspec/changes/phase8-release-readiness-convergence/tasks.md` still has 4.5 unchecked for remote reconciliation/push/hosted-CI inspection, while later campaigns and current workstate record later mainline pushes and hosted runs.
   These are examples of potential stale historical frontier items; prove disposition from Git/evidence before changing them.
9. External tasks in older ledgers—production signer/pins, privileged MFT/USN, clean-host validation, physical/interactive display matrices, guest/provider evidence—must **not** be auto-closed just to reduce the task count.
10. PR #6 remains stale/superseded audit evidence. Do not merge or cherry-pick it wholesale.

## Required reconnaissance before edits

Do not start from the planner snapshot without reconciliation.

1. `git fetch --prune origin`.
2. Record current branch, local `HEAD`, `origin/main`, ahead/behind counts, worktree state, and recent commits.
3. Start from the actual current `main`; fast-forward/rebase safely as repository policy allows. Never destroy valid work and never force-push.
4. Re-read at minimum:
   - `AGENTS.md`
   - `AUTONOMOUS.md`
   - `.agent/PLANNER_HANDOFF.md`
   - this `.agent/EXECUTION_PROMPT.md`
   - `progress.md`
   - `docs/agent/WORKSTATE.md`
   - `README.md`
   - `CODE_INDEX.md`
   - `.github/workflows/ci.yml`
   - `openspec/config.yaml`
   - every active-looking `openspec/changes/**/tasks.md` that contributes unchecked tasks
   - `openspec/changes/phase8-release-readiness-convergence/**`
   - `openspec/changes/real-user-dogfooding-release-engineering/**`
   - `openspec/changes/runtime-certification-and-verifier-convergence/**`
   - `verify/core/ProjectStatus.psm1`
   - `verify/status/Get-ProjectStatus.ps1`
   - `verify/status/New-ReleaseReadinessReport.ps1`
   - `verify/status/tests/Test-ProjectStatus.ps1`
   - `verify/schemas/project-status.schema.json`
   - `verify/schemas/release-readiness-frontier.schema.json`
   - `verify/verify.ps1`
   - any doc-drift/status-drift/policy tests that consume generated status or OpenSpec task counts.
5. Inspect open PRs/issues. PR #6 is evidence only unless current code proves otherwise.
6. Generate/inspect the current project status from the actual tree before changing classification. Record the exact list of `localOpen` items and which change/task produced each one.
7. For each purported local-open task, inspect its surrounding change proposal/design/evidence and relevant later commits. Do not infer current work from the checkbox alone.

Use the repository's native tools and available agent environment. Do not assume a specific AI model, coding harness, shell wrapper, or provider.

## Fresh campaign ledger

Create a new OpenSpec change named equivalently to:

`openspec-frontier-lifecycle-truthfulness`

Include at least:

- `.openspec.yaml`
- `proposal.md`
- `design.md`
- `tasks.md`
- appropriate spec delta/new spec for repository-status/frontier semantics
- `evidence/baseline.md`
- `evidence/historical-ledger-reconciliation.md`
- `evidence/implementation-and-validation.md`

The campaign evidence must distinguish:

1. currently active local implementation work;
2. completed historical work whose old task ledger was not reconciled at the time;
3. superseded work whose requirement is satisfied by a newer implementation/campaign;
4. genuinely open external evidence gates;
5. environment-blocked interactive/elevated tasks;
6. ambiguous tasks that cannot be safely reclassified without stronger evidence.

Never erase historical provenance. If a historical task is checked because later evidence now proves it complete, annotate it with the evidence/commit/run that justifies the change.

## Workstream 1 — Inventory the real OpenSpec frontier

Build a deterministic inventory of every unchecked OpenSpec task in the current tree.

For each unchecked task capture at least:

- change name;
- task ID and exact source line;
- task text;
- change creation date if available;
- whether the change is currently active, completed, superseded, historical, or external-only;
- whether later commits/evidence satisfy the task;
- category: local / interactive / elevated / external-provider / production-signing / clean-host / administrative;
- evidence source used for the disposition;
- whether the task should remain open, be checked with evidence, or be excluded from the *active* frontier while preserved historically.

Do not mass-edit task files before this inventory exists.

Explicitly examine historical delivery/checkpoint tasks such as "reconcile with origin/main / push / inspect hosted CI". A later successful push or hosted run can satisfy such a task, but only if the relevant campaign's delivery requirement can be tied to real Git/CI evidence.

## Workstream 2 — Introduce explicit lifecycle semantics

The status system must stop relying on "unchecked anywhere under `openspec/changes` == active frontier".

Design and implement an explicit, machine-readable lifecycle model for OpenSpec changes. Prefer repository/OpenSpec-native semantics if the installed OpenSpec workflow already provides a correct archive/completion mechanism; otherwise introduce the smallest compatible repository-owned metadata needed.

Required lifecycle concepts:

- active implementation;
- completed/historical;
- superseded by a named newer change;
- external-evidence-only / locally complete but release evidence pending, when necessary;
- invalid/unknown lifecycle must fail closed or remain visible rather than silently disappearing.

Requirements:

- lifecycle state must not be inferred from directory name, creation timestamp, latest commit message, or prose-only wording;
- lifecycle metadata must be schema-validated or otherwise deterministically validated;
- a completed/historical change's unchecked external tasks may remain historically visible without being counted as current local implementation work;
- an active change with a genuine unchecked local task must still appear in `localOpen`;
- a completed change with a task proven complete later must not continue to poison active-frontier counts;
- superseded work must point to the superseding change/evidence rather than simply disappearing;
- malformed lifecycle metadata must be reported as a status/model error, not guessed around.

Do not bulk archive/move changes unless the repository's OpenSpec contract supports it and all synced-spec implications are understood. If archival is selected, validate that specs, evidence links, doc-drift tooling, CI, and historical references remain intact.

## Workstream 3 — Replace fragile text-only task classification

Audit `Get-ProjectStatusTaskClassification` and every consumer.

The current keyword classifier can remain as a fallback diagnostic, but it must not be the sole authority for task lifecycle or whether a task is currently actionable.

Harden the model so that:

- active-vs-historical is explicit;
- category can be explicitly declared where needed;
- low-confidence text inference is exposed as such and cannot silently promote historical work to current local work;
- administrative Git/CI handoff tasks are not mistaken for product implementation merely because they lack a signing/interactive keyword;
- mixed tasks with local + external clauses are not flattened into an incorrect single state;
- external evidence remains visible and actionable without becoming fake implementation debt;
- task source/evidence/lifecycle provenance survives into JSON and Markdown projections.

If task-level metadata is introduced, keep Markdown readable and preserve compatibility with existing OpenSpec validation. Do not turn every historical task into a giant metadata rewrite if a cleaner change-level lifecycle solution is sufficient.

## Workstream 4 — Reconcile historical ledgers with evidence

After the lifecycle mechanism exists, systematically reconcile stale historical task state.

At minimum inspect:

- `phase8-release-readiness-convergence`;
- `real-user-dogfooding-release-engineering`;
- `runtime-certification-and-verifier-convergence`;
- `ship-ready-ui-ux-hardening-convergence`;
- `interactive-ui-runtime-determinism`;
- `active-path-runtime-resilience`;
- older release/validation changes that still contribute unchecked tasks to generated status.

For each historical unchecked local/admin task:

1. prove whether it was completed by later Git/CI/evidence;
2. if proven, check it off and annotate with exact later evidence;
3. if not proven, keep it open but classify it honestly under the lifecycle model;
4. if superseded, record the superseding change/commit and preserve the original requirement;
5. never check an external signing/privileged/clean-host/interactive/provider task without actual evidence.

Do **not** chase task-count zero. The goal is truthful state, not cosmetic completion.

## Workstream 5 — Project-status and release-frontier convergence

Update the complete status pipeline, not only one helper.

Audit and harden:

- `Get-ProjectStatusOpenSpecFrontier`;
- `Get-ProjectStatusTaskClassification`;
- `New-ProjectStatus` frontier buckets;
- project-status JSON schema;
- release-readiness-frontier schema/generation;
- Markdown projection;
- `verify.ps1 status` output;
- status/release claim-consistency checks;
- any CI artifact generation based on the frontier;
- any planner/agent logic that consumes local-open counts or durable state.

Required properties:

- `localOpen` means genuinely active locally actionable work, not "some old checkbox is empty";
- completed historical changes do not inflate active-local counts;
- unresolved external gates stay visible in the correct blocked bucket;
- active campaigns remain visible even if most tasks are blocked;
- lifecycle/model errors are first-class failures;
- generated status remains deterministic and free of host-private paths;
- release readiness never becomes production-ready because external tasks were hidden or archived;
- a historical task cannot be silently dropped without lifecycle/evidence provenance.

If the JSON schema changes, version it deliberately and keep consumers/tests synchronized.

## Workstream 6 — Autonomous continuation / planner safety

This status data directly affects future autonomous work selection. Audit the planner/executor handoff around it.

Ensure that:

- `/goal continue` resumes the currently ACTIVE planner prompt or native active campaign rather than resurrecting a completed historical OpenSpec change;
- when no active local implementation frontier remains, the system can truthfully say so while still listing external release gates;
- `next-campaign` planning can distinguish "no local work" from "many historical unchecked tasks";
- a completed `.agent/EXECUTION_PROMPT.md` cannot be treated as active solely because an old OpenSpec task remains unchecked;
- the new campaign marks itself complete at final handoff and does not leave a self-referential planner loop.

Keep this general-purpose. Do not hard-code OpenCode, Codex, Kimi, Pi, a model name, or any particular agent harness.

## Workstream 7 — Deterministic regression suite

Add focused tests that reproduce the failure mode without needing a live desktop, elevation, signing material, or network access.

At minimum cover fixture repositories/change trees with:

1. one active change with one local unchecked task -> appears in `localOpen`;
2. one completed historical change with an unchecked external evidence task -> does not appear as current local work, but remains visible as historical/external evidence;
3. one completed historical change with a stale unchecked remote-push/CI task that later evidence satisfies -> reconciles deterministically and no longer appears local-open;
4. one superseded change -> preserved with supersession provenance and excluded from active implementation frontier;
5. malformed/unknown lifecycle metadata -> explicit failure/diagnostic;
6. a real external signing task -> production-signing blocked;
7. interactive/elevated/provider/clean-host tasks -> correct blocked buckets;
8. a task whose prose contains misleading keywords -> explicit metadata/lifecycle wins over regex guessing;
9. mixed local+external wording -> no incorrect flattening;
10. stable JSON/Markdown ordering across repeated runs;
11. safe path redaction remains intact;
12. release-readiness composition never infers production PASS from lifecycle filtering.

Extend `verify/status/tests/Test-ProjectStatus.ps1` or split tests if that keeps the seam cleaner. Register all new deterministic tests in CTest/CI as appropriate.

## Workstream 8 — Repository-wide impact audit

Do not limit review to `ProjectStatus.psm1`.

Trace the change through the whole affected system:

- OpenSpec validation/config and task ledgers;
- generated project status;
- release-readiness generation;
- doc-drift / status-drift tests;
- CI status/release artifacts;
- `.agent/PLANNER_HANDOFF.md` and `.agent/EXECUTION_PROMPT.md` lifecycle;
- `progress.md` / `docs/agent/WORKSTATE.md`;
- `verify/README.md` and status command documentation;
- release candidate/readiness reporting;
- any scripts/tests that assume `totalOpenTasks` means active work;
- any automation that parses exact schema fields or frontier bucket names.

Search for `totalOpenTasks`, `changesWithOpenTasks`, `localOpen`, `Get-ProjectStatusOpenSpecFrontier`, project-status schema fields, and direct scans of `tasks.md`. Update every affected consumer or prove compatibility.

Also audit how the latest runtime-certification result-model changes interact with status generation. Preserve the explicit selected-run identity and fail-closed result contradiction behavior already implemented.

## Workstream 9 — Validation

Run focused tests continuously, then the broad local gates. Fix all locally actionable regressions introduced or exposed by this campaign.

Required final validation, when supported by the current host:

1. PowerShell parser/static checks for modified verification scripts.
2. Focused project-status/lifecycle/frontier fixture tests.
3. Schema validation for project-status and release-readiness outputs.
4. Strict OpenSpec validation for all changes/specs.
5. `pwsh ./verify/verify.ps1 status` and release-readiness generation from a clean tree.
6. `pwsh ./verify/verify.ps1 doctor`.
7. `pwsh ./verify/verify.ps1 build`.
8. Configure/build Debug through the repository's supported VS environment.
9. Full Debug CTest.
10. Configure/build release-test and full release-test CTest.
11. Analyze configure/build with no new warnings.
12. Applicable ASAN protocol/IPC slice only; do not expand ASAN for optics.
13. Source-marker and diagnostic-policy gates.
14. Doc-drift/status-drift tests.
15. `git diff --check`.
16. Generate the final project-status JSON/Markdown and manually verify the active-local frontier matches the reconciled evidence ledger.
17. Inspect hosted CI for the exact pushed implementation SHA and fix any locally actionable failure.

Do **not** rerun the full live UIA/repeatability matrix merely to satisfy this campaign. The current workstate explicitly records host resource/interactive limits. Only run those live paths if the host condition materially changes or this campaign exposes a new bounded reason to do so.

## Preserved behavior / non-negotiable constraints

Do not regress or weaken:

- three-process privileged/unprivileged architecture;
- closed protocol and frame-size bounds;
- symmetric pinned Authenticode verification;
- fail-closed production signing/pin behavior;
- service ACL/SCM restrictions and DLL hardening;
- no forwarded file content across the privileged seam;
- immutable snapshot/publication semantics;
- degraded mode as a first-class supported path;
- exact canonical navigation/file-operation identity;
- search cancellation/supersession and nonblocking indexed read-plane semantics;
- UIA fragment lifetime and stale-provider rejection;
- High Contrast/native accessibility behavior;
- exact owned-process cleanup and no host-wide process killing;
- no trust-store mutation during ordinary development;
- truthful distinction between local PASS, environment block, external prerequisite, and production certification.

PR #6 remains stale evidence only. Do not merge/reset/cherry-pick it wholesale.

## Acceptance criteria

This campaign is complete only when all of the following are true:

- the current false/stale OpenSpec frontier behavior is reproduced with concrete examples from the repository or equivalent deterministic fixtures;
- an explicit lifecycle model exists and is validated;
- current local work is distinguished from historical/completed/superseded/external work without prose-only guessing;
- historical task ledgers are reconciled only where real later evidence supports the disposition;
- genuine signing/privileged/clean-host/provider/interactive gates remain open and visible;
- generated `localOpen` contains only genuinely active locally actionable work;
- project-status and release-readiness JSON/Markdown agree on lifecycle, category, counts, and provenance;
- schema consumers, CI, doc/status drift, and autonomous continuation semantics remain consistent;
- deterministic tests cover lifecycle filtering, stale historical tasks, supersession, malformed metadata, external buckets, misleading keyword prose, and stable projection ordering;
- broad Debug/release-test/Analyze/ASAN/OpenSpec/verifier/policy validation is green for all locally runnable gates;
- no product/security behavior was weakened to make the frontier look clean;
- the campaign's durable OpenSpec evidence and workstate are updated from final Git reality;
- final implementation is committed to `main`, reconciled with `origin/main`, pushed without force, exact hosted CI is inspected, and the worktree/remote state are clean and equal.

## Git and handoff requirements

Work on the actual current `main` unless repository-native state requires a safe temporary branch for isolation; final accepted work must land on `main` according to repository policy.

At final delivery:

1. inspect the complete diff and generated status;
2. run the final validation ladder;
3. update the new OpenSpec ledger, `progress.md`, and `docs/agent/WORKSTATE.md` with evidence-backed state;
4. mark this `.agent/EXECUTION_PROMPT.md` **COMPLETE** only after the implementation acceptance criteria are met;
5. commit with a detailed session-report message describing root cause, lifecycle model, historical reconciliation, tests, external boundaries, and remaining truthful blockers;
6. fetch/prune and reconcile safely;
7. never force-push;
8. push all accepted work to `origin/main`;
9. verify local `HEAD == origin/main` and the worktree is clean;
10. inspect hosted CI for the exact pushed SHA;
11. leave the repository so the next `/goal continue` reads durable state and can proceed without any prompt pasted through chat.

Do not output a new pasteable prompt at the end of execution. Execute the campaign, update durable repository state, and hand off through Git/OpenSpec/workstate only.
