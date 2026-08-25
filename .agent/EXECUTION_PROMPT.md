# NEXT CAMPAIGN — FASTFILES

Status: **ACTIVE**
Planned-From: `ff4a6fec279e8b881fe9e20b471e2e5b4a2d92f6`
Planned-At: `2026-08-25T21:26:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **OpenSpec Lifecycle Evidence Integrity + Semantic Consistency Hardening**

## Mission

Harden the lifecycle-aware OpenSpec frontier so a future autonomous planner cannot hide real local work, misclassify release blockers, or accept a false completed/superseded state merely because `openspec/lifecycle.json` is structurally well-formed.

The previous lifecycle-truthfulness campaign successfully removed stale historical checkboxes from the active local frontier and left current `main` with **24 raw unchecked tasks and 0 active-local tasks**. That campaign is complete. There is no justified product-feature implementation campaign to queue from the current repository state, and draft PR #6 remains superseded audit evidence rather than merge work.

The next locally actionable hardening frontier is therefore the **integrity of the lifecycle authority itself**. The current model validates names, enum values, task IDs, non-empty evidence arrays, and basic supersession target existence, but evidence remains an untyped string list and semantic relationships between lifecycle state, task disposition, category, status, evidence, and supersession are not comprehensively enforced. The deterministic tests intentionally use arbitrary strings such as `fixture-history`, `fixture-supersession`, and `fixture-current` as accepted evidence. A malformed JSON document fails closed, but a syntactically valid manifest can still carry non-resolving evidence or internally contradictory lifecycle claims.

Close that trust gap without adding network dependence to ordinary status generation, without weakening any production-security or release gate, and without manufacturing unrelated product churn.

## Planner findings that must be independently revalidated

Treat these as starting evidence, not unquestionable truth. Reconcile against live `main` before editing.

1. Remote `main` currently points to `ff4a6fec279e8b881fe9e20b471e2e5b4a2d92f6` (`status: close lifecycle frontier handoff`).
2. `.agent/EXECUTION_PROMPT.md` at that baseline is COMPLETE for `openspec-frontier-lifecycle-truthfulness`; `progress.md` and `docs/agent/WORKSTATE.md` report 24 raw unchecked tasks, 0 active-local tasks, lifecycle model PASS, and green local/hosted evidence for the implementation commit.
3. The previous implementation commit is `2b600eb9a8d1f8d4e20168653da50afcddc97717` (`status: make OpenSpec frontier lifecycle-aware`).
4. `openspec/lifecycle.json` is now the repository-owned machine-readable lifecycle source used to separate raw unchecked-task truth from the active local frontier.
5. `verify/schemas/openspec-lifecycle.schema.json` models lifecycle `evidence` and task-override `evidence` as arrays of unrestricted strings. The schema checks shape, not referential integrity.
6. `Get-ProjectStatusLifecycleModel` in `verify/core/ProjectStatus.psm1` currently verifies that lifecycle evidence arrays exist and contain non-empty strings. It does not, by itself, prove that an evidence reference resolves to a repository artifact, Git commit, durable hosted-run record, or another accepted evidence type.
7. The same function checks that a `supersededBy` name exists, but the planner review found no explicit prevention of self-supersession, supersession cycles, or other graph-level contradictions. Revalidate this across the complete implementation and tests before changing it.
8. Task overrides validate individual enum membership but the planner review found no complete cross-field semantic validator preventing contradictory combinations such as an `external` disposition with only `local` categories, a `mixed` disposition without both local and external components, an impossible lifecycle/disposition relationship, or a stale unchecked task being labeled PASS without evidence strong enough to justify that claim. Revalidate the exact current behavior before edits.
9. `verify/status/tests/Test-ProjectStatus.ps1` has good coverage for active-local, historical-external, stale administrative, superseded, malformed metadata, explicit-vs-prose classification, mixed work, blocked buckets, deterministic ordering, schema validation, safe redaction, and release-readiness composition. Its lifecycle fixtures deliberately use free-form strings such as `fixture-history`, `fixture-supersession`, `fixture-current`, and `fixture-local-evidence`, which demonstrates that evidence *presence* is tested but evidence *resolution/integrity* is not yet a first-class invariant.
10. The lifecycle-truthfulness design explicitly identifies stale manifest state as a risk and says lifecycle/evidence provenance must be reviewable. This campaign must make that guarantee executable rather than purely documentary.
11. Release-readiness and autonomous continuation consume the lifecycle projection. A false lifecycle PASS is therefore not a cosmetic documentation problem; it can alter whether future work is selected and whether blockers are projected correctly.
12. The repository currently has no open issues. Draft PR #6 (`stabilization/ship-ready-2026-08-23`) remains open but superseded by later mainline hardening; it is evidence only unless current code independently proves a still-valid defect.
13. Existing production signer/pins, privileged MFT/USN validation, clean-host matrix, guest/provider validation, physical-display/foreground-owned UIA, and other host-bound gates remain external. Do not relabel them as local work just to make this campaign larger.

## Mandatory reconnaissance before edits

Do not start from the planner snapshot without live reconciliation.

1. `git fetch --prune origin`.
2. Record current branch, local `HEAD`, `origin/main`, ahead/behind counts, worktree state, and recent commits.
3. Start from the actual current `main`. Fast-forward/rebase safely as repository policy permits. Never destroy valid work and never force-push.
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
   - `openspec/lifecycle.json`
   - `verify/schemas/openspec-lifecycle.schema.json`
   - `verify/schemas/project-status.schema.json`
   - `verify/schemas/release-readiness-frontier.schema.json`
   - `verify/core/ProjectStatus.psm1`
   - `verify/core/Schema.psm1`
   - `verify/status/Get-ProjectStatus.ps1`
   - `verify/status/New-ReleaseReadinessReport.ps1`
   - `verify/status/tests/Test-ProjectStatus.ps1`
   - `verify/verify.ps1`
   - all CMake/CTest registration for status/policy/doc-drift tests
   - all repository docs and scripts that consume `frontier.localOpen`, lifecycle status, raw unchecked counts, lifecycle evidence, or `supersededBy`.
5. Inspect recent lifecycle-related commits and their complete diffs, at minimum:
   - `b0ed5803b0aeba452822208b5230dd12e00f3832`
   - `2b600eb9a8d1f8d4e20168653da50afcddc97717`
   - `ff4a6fec279e8b881fe9e20b471e2e5b4a2d92f6`
6. Inspect open PRs/issues. PR #6 is historical/superseded evidence only unless an independently reproduced current-main defect requires action.
7. Generate current project status and release-readiness from the actual tree before editing. Record raw unchecked tasks, active-local count, lifecycle errors, blocked buckets, and the current candidate classification.
8. Build an impact map of **every consumer** of lifecycle state/evidence. Do not limit the audit to files changed in the previous campaign. Trace status generation through release-readiness, Markdown/JSON projection, verifier commands, CI artifacts, autonomous/planner documentation, and any tests or policies that make decisions from these fields.
9. Audit the complete current lifecycle manifest entry-by-entry. For every lifecycle and task-level evidence string, determine what it is intended to mean: repository path, directory, commit, hosted run, durable workstate record, task ledger, prompt, or other provenance. Record ambiguity instead of guessing.
10. Do not read or glob ignored `verify/runs/`, `verify/baselines/`, or `verify/.signing/`.

Use the repository's native tools and available agent environment. Do not assume a specific AI model, coding harness, shell wrapper, provider, or sub-agent implementation.

## Fresh campaign ledger

Create a new OpenSpec change named equivalently to:

`openspec-lifecycle-evidence-integrity-and-semantic-convergence`

Include at least:

- `.openspec.yaml`
- `proposal.md`
- `design.md`
- `tasks.md`
- appropriate spec delta/new spec for lifecycle integrity semantics
- `evidence/baseline.md`
- `evidence/current-manifest-audit.md`
- `evidence/evidence-reference-model.md`
- `evidence/semantic-invariant-matrix.md`
- `evidence/implementation-and-validation.md`

The baseline must preserve exact pre-change Git/status/release facts and must inventory the current lifecycle manifest rather than simply state that it passed.

## Workstream 1 — Repository-wide lifecycle trust-boundary audit

Treat lifecycle metadata as a trust boundary because it can influence autonomous work selection and release-state projection.

Build a deterministic audit covering:

- the manifest schema;
- runtime parser/validator;
- task classifier;
- frontier projection;
- release-readiness projection;
- JSON/Markdown schemas and rendering;
- verifier command failure behavior;
- CI generation/validation;
- planner/continuation documentation;
- historical task reconciliation paths;
- all current lifecycle entries and overrides;
- all code paths that consume lifecycle evidence or supersession.

For each seam, state:

1. what input is trusted today;
2. what is syntactically validated;
3. what is semantically validated;
4. what is only human convention;
5. what failure mode occurs if metadata lies or drifts;
6. whether the system fails closed, fails open, or silently misprojects state.

Search the full codebase for lifecycle/status/frontier/release terminology and review effects on unrelated modules only where they consume the changed contract. The audit must account for the system as a whole, not just the recent diff.

## Workstream 2 — Introduce an explicit evidence reference model

Replace the current "non-empty arbitrary string means evidence exists" assumption with the smallest deterministic evidence model that can be validated offline.

Design before implementation. Requirements:

- Evidence must be machine-classifiable, not inferred from string shape when that would be ambiguous.
- Ordinary `verify.ps1 status` and release-readiness generation must remain offline and deterministic; do **not** add a GitHub/network dependency to status generation.
- Evidence that claims a repository file/directory must resolve inside the repository and must not escape via absolute path, `..`, symlink/reparse ambiguity, or host-private path.
- Evidence that claims a Git commit must resolve to an actual commit reachable from the repository according to the chosen contract. Prefer full immutable SHAs in durable state; if abbreviated SHAs remain accepted, require unique deterministic resolution.
- Hosted-CI evidence must not be treated as locally re-queryable truth. Define a durable offline representation that binds the run identifier to an exact commit and to a committed evidence/workstate record. A bare numeric run ID must not become authoritative merely because it is non-empty.
- Human notes may remain useful provenance, but a free-form note alone must not satisfy the evidence requirement for a state transition that can remove work from the active frontier.
- The model must distinguish "reference exists" from "reference proves this claim". At minimum, provenance strength/type must be visible to reviewers and tests.
- Evidence resolution must be deterministic across machines and must not expose absolute user paths.
- Existing historical provenance must be migrated without deleting or rewriting history beyond what is necessary for a truthful current manifest.
- If a structured evidence object is introduced, version the lifecycle schema deliberately and update all consumers atomically.

Possible evidence kinds to evaluate include repository artifact/path, Git commit, durable hosted-run binding, and explicitly non-authoritative note/context. Do not hard-code this exact taxonomy if a cleaner repository-native model exists; the acceptance requirement is typed, reviewable, offline-verifiable provenance.

## Workstream 3 — Enforce lifecycle semantic invariants

Add a dedicated semantic validation layer rather than relying only on JSON Schema enum membership.

At minimum evaluate and, where appropriate, enforce:

### Change-level invariants

- `supersededBy` cannot point to the same change.
- Supersession relationships must be acyclic.
- Supersession targets must exist and have a lifecycle state compatible with being an authoritative replacement.
- A completed/superseded state that removes local work from the active frontier must carry evidence strong enough for that transition under the new evidence model.
- `external-evidence-only` means local implementation is complete but named external evidence remains; it must not become a generic escape hatch for hiding unexplained local work.
- `ambiguous` remains fail-closed/visible and cannot project a false clean frontier.
- Unknown or contradictory lifecycle state must fail the lifecycle model, preserve raw task visibility, and prevent production-ready inference.

### Task-override invariants

Audit the exact valid matrix before coding, then enforce it deterministically. Cover at minimum:

- `active` disposition is only valid when the parent lifecycle can contain current work.
- `external` disposition requires one or more external/environmental/administrative categories; a purely `local` category is contradictory.
- `mixed` disposition must actually represent both a local component and at least one non-local/external component.
- `superseded` disposition requires supersession provenance and must not silently disappear from historical projection.
- `historical` disposition requires durable provenance showing why an unchecked source task is no longer current implementation work.
- A still-unchecked task marked `PASS` must have a clearly defined meaning and sufficient evidence. Do not allow PASS to become a cosmetic escape from an unchecked acceptance item.
- Category/status combinations must be coherent (`BLOCKED_ENVIRONMENT`, `REQUIRED-BUT-UNAVAILABLE`, `SKIPPED`, `PASS`, `FAIL`) and must not undermine release-state truth.
- Explicit metadata may override prose classification only when the override itself passes semantic validation.

Do not over-constrain legitimate historical cases. Preserve the ability to represent a task whose implementation is locally complete while external validation remains pending.

## Workstream 4 — Evidence freshness and drift detection

A reference can exist and still be stale. Add the strongest deterministic drift checks that can be justified without network dependence or false positives.

Evaluate at least:

- manifest entries referring to deleted/renamed repository evidence;
- task overrides referring to task IDs that changed or disappeared;
- lifecycle evidence pointing to a commit that does not contain or precede the claimed artifact in a meaningful way;
- supersession targets whose own lifecycle becomes ambiguous/invalid;
- completed/external-only changes that later acquire a new unchecked local task without corresponding lifecycle/task metadata;
- active planner prompt versus lifecycle manifest disagreement;
- multiple sources claiming incompatible active/completed state for the same campaign;
- schema version drift between lifecycle parser, JSON Schema, project-status schema, release-readiness schema, tests, and docs.

Where exact semantic freshness cannot be proven mechanically, fail closed or surface an explicit diagnostic rather than guessing.

Do not create a timestamp-based freshness heuristic. Lifecycle truth must come from repository state and evidence, not age.

## Workstream 5 — Migrate the current manifest truthfully

After the new model exists, migrate the live `openspec/lifecycle.json` and any required evidence records.

For every current entry:

1. normalize evidence into the new model;
2. resolve every repository artifact reference;
3. resolve every Git commit reference;
4. bind hosted-run claims to durable exact-SHA evidence where available;
5. verify supersession relations;
6. verify task overrides against actual `tasks.md` lines;
7. preserve external signing/provider/clean-host/elevated/interactive boundaries;
8. preserve historical task provenance;
9. leave ambiguous claims visible rather than manufacturing proof.

Specifically revalidate the current entries for:

- `autonomous-runtime-verification`
- `close-independent-validation-gaps`
- `interactive-ui-runtime-determinism`
- `nonblocking-indexed-read-plane`
- `phase8-release-readiness-convergence`
- `real-user-dogfooding-release-engineering`
- `runtime-certification-and-verifier-convergence`
- `ship-ready-ui-ux-hardening-convergence`
- `zero-touch-autonomous-engineering`
- `openspec-frontier-lifecycle-truthfulness`
- `stabilize-live-uia-e2e`

Do not chase a raw unchecked-task count of zero. The target is integrity, not cosmetic closure.

## Workstream 6 — Harden project-status and release-readiness consumers

Update the complete consumer graph so integrity failures cannot be lost downstream.

Required properties:

- lifecycle structural errors and semantic-integrity errors are separately diagnosable;
- `frontier.localOpen` is emitted only when the lifecycle model passes the new integrity checks;
- raw unchecked tasks remain visible even when lifecycle validation fails;
- ambiguous or invalid evidence cannot cause a task to disappear from all actionable/historical/blocked projections;
- release-readiness remains non-production/blocked when lifecycle integrity is invalid;
- a lifecycle model failure cannot be converted into PASS/SKIPPED by a renderer or wrapper;
- JSON and Markdown expose enough evidence type/source/provenance to audit why work was excluded from the active frontier;
- generated output remains deterministic and path-safe;
- no consumer silently interprets old schema-v1 strings as trusted evidence after migration unless an explicit compatibility path is designed and tested.

Version project-status/release-frontier schemas only if the external shape changes. If versions change, update every producer/consumer/test atomically.

## Workstream 7 — Autonomous continuation safety

Re-audit planner/executor semantics after evidence hardening.

Ensure that:

- `/goal continue` or equivalent resumes an ACTIVE planner prompt only after current Git/lifecycle reconciliation;
- a completed planner prompt cannot be resurrected by stale historical tasks;
- an ACTIVE prompt cannot be silently ignored because a bad lifecycle edit marks its change completed;
- `next-campaign` can distinguish "no local work" from "lifecycle integrity failure" and from "external-only blockers";
- lifecycle integrity failure causes a repair/hardening action, not a false STOP state;
- the system remains general-purpose and does not hard-code OpenCode, Codex, Kimi, Pi, a model, or a provider.

Do not build a new orchestrator. Harden the repository contract that existing agents consume.

## Workstream 8 — Deterministic regression suite

Extend the status/lifecycle test suite with repository-independent fixtures. No live desktop, elevation, signer, provider, or network access should be required.

At minimum add cases for:

1. valid repository-path evidence resolves and passes;
2. missing repository-path evidence fails lifecycle integrity;
3. path traversal / absolute host path / unsafe evidence reference is rejected or sanitized according to the contract;
4. valid full Git commit evidence resolves;
5. nonexistent Git commit evidence fails;
6. ambiguous abbreviated commit evidence fails if abbreviated SHAs remain accepted;
7. hosted-run evidence without an exact commit/durable record is non-authoritative or fails according to the designed contract;
8. a valid durable hosted-run binding passes offline;
9. free-form note alone cannot justify completed/superseded exclusion of local work;
10. self-supersession fails;
11. two-node supersession cycle fails;
12. longer supersession cycle fails;
13. valid acyclic supersession chain preserves provenance;
14. supersession to an unknown/invalid target fails;
15. `external` + only `local` category fails;
16. `mixed` without a local component fails;
17. `mixed` without an external/environmental component fails;
18. impossible disposition/status combination fails;
19. unchecked historical/PASS task without sufficient evidence fails or remains visible according to the designed semantics;
20. explicit valid metadata still wins over misleading prose;
21. a newly added unchecked local task to an external-only/completed change cannot be silently hidden;
22. malformed lifecycle metadata remains a first-class failure;
23. integrity failure preserves raw unchecked tasks and projects them as ambiguous/integrity-blocked rather than dropping them;
24. project-status schema and release-readiness schema remain synchronized;
25. release-readiness cannot become production-ready when lifecycle integrity fails;
26. repeated generation has stable ordering/output;
27. evidence paths remain repository-relative/redacted;
28. the live migrated manifest validates structurally and semantically.

Tests must assert exact reason codes where practical so future regressions are diagnosable rather than merely nonzero.

## Workstream 9 — Repository-wide regression/compatibility audit

After implementation, inspect the entire repository for consequences of the new lifecycle contract.

At minimum audit:

- all PowerShell modules/scripts importing project-status helpers;
- status/release schema readers;
- verifier capabilities/manifests;
- CI jobs and artifacts;
- CMake/CTest registrations;
- doc-drift/policy/source-marker tests;
- OpenSpec validators;
- planner/handoff files;
- docs containing old schema-v1 examples or claims;
- any code that assumes lifecycle evidence is a string;
- any code that assumes lifecycle PASS means only syntactic validity;
- any code that reads `activeLocalOpenTasks` without checking lifecycle integrity status.

Do not modify unrelated product/UI/index/security code unless the audit proves a direct contract break caused by this campaign.

## Workstream 10 — Validation ladder and final convergence

Run focused validation first, then the broad repository gates.

Required locally applicable validation:

1. PowerShell parser checks for every modified script/module.
2. Focused lifecycle/project-status tests.
3. Project-status and release-readiness generation from the real repository.
4. Strict validation of the new OpenSpec change and all existing changes.
5. Debug configure/build and full Debug CTest.
6. `release-test` configure/build and full release-test CTest.
7. Analyze configure/build with warnings clean.
8. ASAN protocol/IPC required slice if repository policy still requires it for campaign closure.
9. `pwsh ./verify/verify.ps1 doctor`.
10. `pwsh ./verify/verify.ps1 build`.
11. status/release-readiness commands and schema validation.
12. source-marker, diagnostic-policy, doc-drift, lifecycle/status policy, and other applicable policy tests.
13. `git diff --check`.
14. Review the complete final diff, not only recently edited files.
15. Check tracked/untracked scope and keep generated run/build/signing artifacts out of commits.

Do **not** rerun host-bound full live UIA, production signer/pin provisioning, privileged MFT/USN, provider/guest, clean-host, or physical-display validation merely because this campaign exists. Those boundaries remain external unless the implementation directly changes those paths, which it should not.

If a broad test exposes a deterministic Critical/High regression caused by this campaign, fix it before handoff. If it exposes a pre-existing unrelated defect, classify it with evidence and decide whether it is a genuine new local frontier rather than silently expanding scope.

## Preserved behavior and non-negotiable invariants

Preserve all of the following:

- Windows-only C++20 product architecture.
- Three-process privileged/unprivileged split.
- Closed privileged command surface.
- Connection-scoped opaque service handles.
- Frame/parser hardening and 1 MiB protocol bound.
- Symmetric signed-peer authentication and production fail-closed signature-pin policy.
- No SCM control rights for clients.
- No file-content forwarding from the privileged scanner.
- DLL search hardening.
- Degraded mode as a first-class supported path.
- No network dependency for ordinary status/release-readiness generation.
- No certificate/trust-store mutation for ordinary development.
- No weakening of production signing, privileged validation, provider, clean-host, or interactive gates.
- No reading/committing ignored verification/signing evidence directories.
- No force-push.

## Out of scope

Unless current-main evidence independently proves otherwise, do not:

- add new product features;
- redesign the file-manager UI;
- rewrite the index/search/storage architecture;
- revive or wholesale merge/cherry-pick draft PR #6;
- provision production certificates or pins;
- mutate trusted root stores;
- require Administrator execution for this hardening campaign;
- manufacture a production-ready claim;
- introduce an online GitHub lookup into `verify.ps1 status`;
- replace OpenSpec wholesale;
- build a new general agent orchestrator;
- perform broad refactors unrelated to lifecycle integrity.

## Acceptance criteria

The campaign is complete only when all applicable criteria below are true:

1. The current repository baseline and pre-change lifecycle frontier are recorded with exact Git evidence.
2. A fresh OpenSpec campaign ledger exists and is strictly valid.
3. Lifecycle evidence has an explicit machine-readable integrity model; arbitrary non-empty strings are no longer sufficient authority for state transitions that hide local work.
4. Repository-path and Git evidence are deterministically validated offline.
5. Hosted-run evidence has a durable exact-SHA binding model and does not require live GitHub access during ordinary status generation.
6. Supersession self-links/cycles/invalid targets are detected.
7. Task override disposition/category/status/evidence relationships are semantically validated.
8. Invalid lifecycle/evidence state fails closed while retaining raw task visibility.
9. Project-status and release-readiness consume the same validated lifecycle model.
10. Lifecycle-integrity failure cannot yield a false zero-work STOP or a false production-ready classification.
11. All current manifest entries are migrated/revalidated without erasing genuine external gates.
12. Deterministic regression fixtures cover missing/bad evidence, supersession cycles, contradictory overrides, drift, schema synchronization, redaction, and release-readiness fail-closed behavior.
13. Focused tests pass.
14. Debug and release-test full suites pass under the supported toolchain, subject only to honestly documented external/environmental boundaries.
15. Analyze is warning-clean and required ASAN/verifier/policy/OpenSpec/diff gates pass.
16. The complete repository-wide consumer audit finds no stale schema-v1/evidence-string assumptions left unhandled.
17. Documentation and durable workstate describe the new contract accurately.
18. No unrelated product/security behavior is weakened.
19. The campaign's local implementation frontier is actually complete at handoff; remaining external tasks stay visible and correctly blocked.
20. The final implementation/evidence commits are pushed to `main` without force and hosted CI for the exact implementation SHA is inspected when available.

## Git and delivery requirements

- Work on current `main` unless repository rules require a safer temporary branch; final durable state must be reconciled with `origin/main` without force.
- Commit logical implementation/evidence waves with descriptive messages.
- Do not commit generated build trees, `verify/runs/`, `verify/baselines/`, `verify/.signing/`, databases, WAL/SHM files, temporary fixtures, or local host artifacts.
- Before each final push, fetch/prune and reconcile with remote changes safely.
- Push without force.
- Inspect hosted CI for the exact implementation SHA when available. Do not create a self-referential documentation commit merely to record the CI result of itself.
- Update `docs/agent/WORKSTATE.md`, `progress.md`, lifecycle/OpenSpec evidence, and this prompt from actual final Git/validation reality.
- Mark this prompt `Status: **COMPLETE**` only after implementation, broad validation, durable evidence, and Git handoff are done.
- The final commit message must be a detailed session report: baseline, root cause, important design choices, files/contracts changed, regression coverage, validation matrix, external boundaries, final state, and next frontier/STOP result.

## Executor continuation contract

Use `/goal continue` or the repository's equivalent autonomous continuation command.

On every resume:

1. reconcile current Git first;
2. re-read repository instructions, planner handoff, this ACTIVE prompt, workstate, and the new OpenSpec tasks;
3. inspect what is genuinely complete rather than replaying the planner snapshot;
4. continue from the first unsatisfied acceptance requirement;
5. perform a repository-wide impact check for every meaningful contract change;
6. fix introduced Critical/High regressions before proceeding;
7. preserve external boundaries rather than fabricating passes;
8. update durable state and commit/push per repository policy.

If this campaign completes and the resulting validated frontier has no active local implementation or hardening work, **STOP truthfully**. Do not invent another campaign merely to keep the loop running. External production signing, privileged MFT/USN, provider/guest, clean-host, physical-display, foreground-owned UIA, and similar evidence gates may remain open without constituting a new local campaign.
