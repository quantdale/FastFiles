# NEXT CAMPAIGN — FASTFILES

Status: **COMPLETE**
Planned-From: `165f7bc7e22fcdc21f58735c38f368769828c409`
Planned-At: `2026-08-26T01:36:00+08:00`
Completed-At: `2026-08-26T14:45:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **Executable Repository Truth + Documentation Drift Convergence**

## Mission

Make FastFiles' repository guidance and architecture/status documentation a trustworthy input to autonomous engineering rather than a manually synchronized narrative that can silently drift while the existing drift gate remains green.

The immediately preceding `openspec-lifecycle-evidence-integrity-and-semantic-convergence` campaign is complete. Current `main` reports **24 raw unchecked tasks and 0 active-local tasks**, lifecycle integrity `PASS`, a green final-handoff CI run, and only external/host-bound signing, privileged, provider, clean-host, guest, physical-display, foreground-owned UIA, and similar evidence gates. There is no justified product-feature campaign to manufacture from that state, and draft PR #6 remains superseded audit evidence rather than merge work.

A repository-wide negative-space audit did, however, find a concrete local hardening gap: **the guidance truth surface can contain materially stale current-state claims while `ffdoc_drift_tests` still passes**. The existing guard only bans four exact historical phrases in four files and checks mixed line endings. It does not verify the semantics that current agents and maintainers actually depend on: current verifier verbs, current OpenSpec/lifecycle state, snapshot-publication architecture, executable/target topology, release/security boundaries, current validation commands, or other duplicated machine-derived facts.

Close that gap. Reconcile current guidance against executable sources, define explicit ownership for machine-derived facts, add deterministic semantic/executable drift checks, and make future contradictory guidance fail locally and in CI. Do this without rewriting completed product architecture, without turning documentation into generated noise, without creating a network dependency, and without relabeling genuine external release prerequisites as local work.

## Planner findings that must be independently revalidated

Treat these as starting evidence, not unquestionable truth. Reconcile against live `main` before editing.

1. Current remote `main` is `165f7bc7e22fcdc21f58735c38f368769828c409` (`openspec-lifecycle: record final integrity handoff`).
2. Hosted Actions run `32867962170` completed successfully for exact final-handoff SHA `165f7bc7e22fcdc21f58735c38f368769828c409`.
3. `progress.md` and `docs/agent/WORKSTATE.md` report the lifecycle-evidence-integrity campaign complete with 24 raw unchecked tasks, 0 active-local tasks, lifecycle PASS, full local validation, and external production/interactive boundaries preserved.
4. `.agent/EXECUTION_PROMPT.md` was COMPLETE for the lifecycle-evidence-integrity campaign before this planner update.
5. The repository currently has no open issues.
6. Draft PR #6 (`stabilization/ship-ready-2026-08-23`, head `9ddf1a3189be32db11c97e7c79bb7404caac1d8f`) remains open and superseded by later mainline work. Do not merge/reset/cherry-pick it wholesale. It is historical comparison evidence only unless a defect is independently reproduced on current `main`.
7. `README.md` currently documents the runtime verifier as exposing only `build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, `list`, and `doctor`, while current repository guidance and implementation also expose `status`, `release-readiness`, and `release-candidate`. Revalidate the actual command registry rather than trusting either document.
8. The same README still says the "current interactive hardening change" is `interactive-ui-runtime-determinism`, even though multiple later campaigns have completed and the lifecycle authority now records the current campaign history explicitly. This is a current-state claim that can misdirect an agent even though the drift gate passes.
9. `CODE_INDEX.md` describes `SnapshotPublisher` in one current section as publishing a "double-buffered memory-mapped section", while current `src/engine/src/SnapshotPublisher.cpp` creates immutable generation-named mappings (`<prefix>.<generation>`), publishes a canonical first generation during `Start`, and commits each generation before handoff. Revalidate all snapshot documentation and the reader side before editing; do not simply replace one phrase from source review.
10. `AGENTS.md`, `README.md`, `CODE_INDEX.md`, `CLAUDE.md`, verifier docs, OpenSpec designs, `.agent` handoff files, and release docs duplicate overlapping facts about architecture, commands, process roles, privileged-path readiness, test presets, snapshot behavior, and active work. Some duplication is useful for humans, but machine-derived claims currently have no comprehensive executable consistency contract.
11. `tests/drift/check_doc_drift.ps1` currently checks only:
    - four exact banned strings (`never LocalSystem`, `SeBackupPrivilege only`, `scan calls are stubbed`, `not yet implemented`) across README/AGENTS/CLAUDE/CODE_INDEX; and
    - mixed CRLF/lone-LF endings under `src/` and `tests/`.
    A green `ffdoc_drift_tests` therefore does **not** prove current guidance matches the actual repository.
12. `tests/drift/CMakeLists.txt` registers this script as `ffdoc_drift_tests` whenever PowerShell is available. Recent campaign reports cite that gate as passing, so the gap is not merely missing CI registration: it is a weak contract that can produce a false-green result.
13. Current source-of-truth seams already exist and should be reused rather than duplicated again: CMake target/test registration, verifier command dispatch/manifests, `VERSION`, `ffprotocol` enums/constants, snapshot-format code, `openspec/lifecycle.json`, OpenSpec task ledgers, `.agent/EXECUTION_PROMPT.md`, release/status schemas, and tracked implementation evidence.
14. The typed lifecycle campaign intentionally made autonomous work selection fail closed on invalid metadata. Guidance files remain outside that equivalent trust boundary even though planners/executors explicitly read them before acting.
15. Existing production signer/pins, privileged MFT/USN execution, privileged trust-store integration, clean-host release matrix, guest/provider validation, and physical/foreground-owned interactive UI evidence remain external or opt-in. Documentation reconciliation must preserve those boundaries exactly.

## Mandatory reconnaissance before edits

Do not start from the planner snapshot without live reconciliation.

1. `git fetch --prune origin`.
2. Record current branch, local `HEAD`, `origin/main`, ahead/behind counts, worktree state, and recent commits.
3. Start from the actual current `main`; fast-forward/rebase safely as repository policy permits. Never discard valid work and never force-push.
4. Re-read at minimum:
   - `AGENTS.md`
   - `AUTONOMOUS.md`
   - `README.md`
   - `CLAUDE.md`
   - `CODE_INDEX.md`
   - `progress.md`
   - `docs/agent/WORKSTATE.md`
   - `.agent/PLANNER_HANDOFF.md`
   - this `.agent/EXECUTION_PROMPT.md`
   - `.github/workflows/ci.yml`
   - root `CMakeLists.txt` and `CMakePresets.json`
   - all `src/**/CMakeLists.txt` and `tests/**/CMakeLists.txt`
   - `tests/drift/check_doc_drift.ps1`
   - `tests/drift/CMakeLists.txt`
   - `verify/verify.ps1`
   - verifier command/core modules and capability manifests used to define supported verbs and states
   - `verify/README.md`
   - status/release-readiness schemas and their generators
   - `openspec/lifecycle.json`
   - the two latest lifecycle campaign proposal/design/tasks/evidence directories
   - snapshot protocol/publisher/reader implementation and tests
   - setup/signing/pin configuration and production-release checks
   - any current docs under `docs/release/`, `docs/quality/`, `docs/agent/`, and `docs/benchmarks/` that restate machine-derived current facts.
5. Inspect the complete diffs of the recent campaigns that materially changed the truth surface, including at minimum:
   - `f51462f9870c0ae577de597e9b531f0bf32ec6b8`
   - `ea15a51298aba2c603152391264767f011fcc3a7`
   - `2b600eb9a8d1f8d4e20168653da50afcddc97717`
   - `cd8b0b1bd60fbb2af25944f172eca1f5b1614684`
   - `09bac46a5371ecaad58b7c38ab044deb2c5d8163`
   - `165f7bc7e22fcdc21f58735c38f368769828c409`.
6. Inspect open PRs/issues. PR #6 is historical/superseded evidence only unless current-main behavior independently demonstrates a remaining defect.
7. Generate current project status and release readiness from the actual tree before editing. Record raw unchecked tasks, active-local count, lifecycle diagnostics, blocked buckets, and candidate classification.
8. Enumerate the complete tracked repository tree and build a coverage ledger for **all current-guidance surfaces and every machine-readable source they restate**. Do not limit review to recently touched files.
9. Search the whole repository for duplicated claims about:
   - supported verifier verbs and exit codes;
   - CMake presets/test presets and required toolchains;
   - executable/static-library target topology;
   - process roles and privilege boundaries;
   - current/active/completed OpenSpec campaigns;
   - snapshot publication/mapping/version behavior;
   - Authenticode pin provisioning and privileged-path readiness;
   - degraded-mode status;
   - CI validation matrix and test registration;
   - product `VERSION` and release artifact identity;
   - production-ready/release-candidate semantics;
   - counts or lists described as current facts.
10. Classify every duplicated claim as one of:
    - **machine-derived** — should be mechanically checked or rendered from an executable source;
    - **stable architectural rationale** — human-maintained but should be anchored to an authoritative design decision;
    - **historical evidence** — may remain immutable and must not be interpreted as current truth;
    - **dynamic status** — should not be copied into durable prose when a live generator already exists;
    - **external prerequisite** — must remain explicit and must not be auto-closed by documentation cleanup.
11. Do not read or glob ignored `verify/runs/`, `verify/baselines/`, or `verify/.signing/`.
12. Use repository-native tools and available environment. Do not assume a specific model, coding harness, shell wrapper, provider, or sub-agent implementation.

## Fresh campaign ledger

Create a new OpenSpec change named equivalently to:

`repository-truth-surface-and-doc-drift-convergence`

Include at least:

- `.openspec.yaml`
- `proposal.md`
- `design.md`
- `tasks.md`
- an appropriate spec delta/new spec for repository truth/drift guarantees
- `evidence/baseline.md`
- `evidence/truth-surface-inventory.md`
- `evidence/authority-map.md`
- `evidence/drift-findings.md`
- `evidence/negative-fixture-matrix.md`
- `evidence/implementation-and-validation.md`

The baseline must pin exact pre-change Git/status/CI facts and list every concrete drift finding found during the full audit. Do not merely copy the planner examples.

## Workstream 1 — Build the complete repository truth-surface inventory

Treat current guidance as an execution input, not cosmetic prose.

Inventory every current-facing document or handoff that a developer/agent can reasonably use to decide what the system is, what is active, how to build/test it, or what is safe to change. At minimum cover:

- README / AGENTS / CLAUDE / CODE_INDEX;
- AUTONOMOUS and `.agent` handoff/continuation files;
- `progress.md` and `docs/agent/WORKSTATE.md`;
- verifier and release documentation;
- current OpenSpec proposal/design/task/evidence files that are explicitly referenced as current authority;
- quality/release/benchmark guidance with current-state assertions.

For every machine-derived assertion, record:

1. the prose claim;
2. the authoritative executable/config source;
3. whether the current claim matches;
4. whether drift can be detected today;
5. what false action a stale claim could cause;
6. whether the claim should remain prose, become derived output, or be replaced by a stable pointer to the source of truth.

Do not turn historical reports into failures merely because they describe old states. The contract must distinguish historical evidence from files that claim to describe **current** behavior.

## Workstream 2 — Define explicit authority ownership instead of adding another duplicate manifest

Design before implementation.

The goal is not to create a second hand-maintained `truth.json` that can itself drift. Reuse existing authoritative seams wherever possible.

Required principles:

- verifier verbs come from the same dispatch/registry the executable wrapper uses, or from a single structured definition consumed by both;
- CMake target/preset/test facts come from CMake/preset sources or a deterministic introspection seam, not a copied list maintained in a PowerShell test;
- OpenSpec current/completed state comes from `openspec/lifecycle.json` plus the active planner prompt contract, not a hard-coded campaign name in README;
- version identity comes from `VERSION` and build metadata sources;
- protocol/snapshot facts come from source constants/contracts or dedicated machine-readable metadata close to the implementation;
- release-security state remains fail-closed and grounded in existing signing/pin/release-readiness logic;
- stable rationale may remain prose, but it must point to an authoritative design artifact rather than duplicating mutable implementation detail unnecessarily.

If a small structured metadata seam is necessary, it must have one clear owner, be consumed by the product/build/verifier or be mechanically derived from them, and have deterministic tests proving it cannot become another independent source of truth.

## Workstream 3 — Replace phrase-ban drift detection with semantic/executable contracts

Harden `ffdoc_drift_tests` or introduce a clearly composed replacement while preserving a fast local/CI gate.

The resulting drift layer must catch meaningful contradictions, not just exact legacy strings.

At minimum implement deterministic checks for the following classes where the audit confirms they are applicable:

### Verifier CLI contract

- current docs must not omit supported stable verbs that they claim to enumerate;
- docs must not advertise unsupported verbs;
- documented exit-code semantics must match the executable contract;
- the check must be derived from the real verifier command definition rather than a manually duplicated list in the test.

### OpenSpec / planner lifecycle contract

- guidance that claims a specific campaign is "current" or "active" must agree with lifecycle/prompt state;
- completed historical campaigns must not be presented as the current local implementation frontier;
- a COMPLETE planner prompt must not be described as active elsewhere, and an ACTIVE planner prompt must be visible to continuation semantics;
- 24 raw unchecked / 0 active-local is a dynamic status fact, not a magic number to hard-code into generic docs.

### Build/test topology contract

- current documented presets/toolchain/test commands must correspond to actual presets/registered supported workflows;
- avoid assertions based on brittle test-count snapshots;
- if a command is opt-in/privileged/production-only, documentation must preserve that classification.

### Process / target architecture contract

- current executable and static-library topology claims must match build ownership sufficiently to detect removed/renamed/added first-party targets that invalidate guidance;
- do not fail on implementation-private helper targets unless a current guidance document claims to enumerate them.

### Snapshot publication contract

- reconcile and protect the current snapshot-generation/publication semantics across source, code index, architecture guidance, and tests;
- do not encode implementation wording into a fragile exact-string assertion. Prefer a structured contract or behavior-level source seam that can be tested.

### Signing / privileged-path contract

- distinguish source fallback placeholders, build-time pin injection, release configure/startup fail-closed behavior, and genuinely unavailable production signer/pin evidence;
- docs must not claim the privileged path is production-validated merely because implementation exists;
- docs must not imply production Release can be built safely without authorized pins/signing prerequisites.

### Historical/current boundary

- historical audit/recovery reports may preserve old claims when clearly historical;
- current guidance must not silently copy historical state forward;
- the guard must scope failures to current-facing surfaces, not rewrite evidence history.

Keep the gate deterministic, offline, path-safe, and fast enough for normal CTest/CI use.

## Workstream 4 — Add adversarial drift fixtures, not only happy-path checks

A drift detector is only trustworthy if tests prove it fails when guidance lies.

Add isolated deterministic fixtures or injectable test seams covering at minimum:

- missing supported verifier verb in a current-doc fixture;
- extra nonexistent verifier verb;
- stale "current campaign" reference;
- lifecycle/prompt disagreement;
- stale snapshot-publication contract;
- incorrect production-signing/degraded-mode classification;
- renamed/removed documented build preset or target where applicable;
- historical document containing old text that must **not** be rejected merely because it is historical;
- malformed/unreadable authority input;
- absolute/private path redaction if generated diagnostic output can include paths;
- deterministic ordering and stable diagnostics.

Tests must prove both false-negative resistance and false-positive discipline. Do not replace four brittle banned phrases with forty brittle banned phrases.

## Workstream 5 — Reconcile all current guidance against actual mainline behavior

After the authority model and tests exist, update the current-facing documents truthfully.

At minimum reconcile:

- README verifier verbs, current OpenSpec wording, build/test/release instructions, and any stale runtime claims;
- AGENTS snapshot/index/publication wording, current normal-development vs privileged-release boundaries, verifier command guidance, and any duplicate implementation facts found stale;
- CODE_INDEX target/dependency map, snapshot publisher/reader semantics, signing/pin wording, verifier/status architecture, and any module descriptions that no longer match source;
- CLAUDE and other harness-specific guidance only where it contains repository facts rather than harness preferences;
- AUTONOMOUS and `.agent` continuation wording where current lifecycle/truth sources are relevant;
- verifier/release docs that enumerate commands/capabilities or current release semantics.

Prefer stable pointers or mechanically checked enumerations over prose that will obviously drift again.

Do **not** erase useful architecture rationale. Do **not** rewrite historical campaign evidence merely to make current docs uniform.

## Workstream 6 — Integrate truth checks into the full verification graph

Trace the complete consumer graph so a semantic drift failure cannot disappear downstream.

Required properties:

- the focused drift contract has one clear command/test entry point;
- CTest registration remains deterministic when PowerShell is available;
- hosted CI executes the semantic drift gate in at least one mandatory path;
- the official verifier/build or policy composition includes it where architecturally appropriate without duplicating execution unnecessarily;
- a drift failure returns a nonzero/fail classification and cannot be rendered as PASS/SKIPPED by wrapper composition;
- diagnostics identify the authority, conflicting claim, and current-facing document without leaking host-private absolute paths;
- CI does not need network access beyond normal checkout to decide documentation truth;
- shallow/full-history requirements must be justified only where Git reachability is actually part of the contract; do not create another unnecessary full-history dependency.

Audit `.github/workflows/ci.yml`, root/test CMake registration, verifier policy/build composition, and any source-marker/doc-drift scripts as one system.

## Workstream 7 — Repository-wide transitive impact and negative-space audit

The campaign is not complete after fixing the planner's three examples.

Before finalizing:

1. enumerate all changed authority seams;
2. find every direct consumer;
3. find unchanged dependents that assume the old shape/wording/behavior;
4. inspect negative space: docs/config/scripts that should consume the authority but do not;
5. inspect tests that can remain green while a consumer is stale;
6. inspect release/readiness/autonomous/planner behavior for unintended coupling;
7. verify no product runtime/security behavior was weakened merely to simplify documentation checks;
8. re-run searches for stale current-state phrases and manually review every hit in context.

If the audit uncovers a genuine Critical/High product defect directly caused by current contract drift, fix it in this campaign only when it is tightly coupled and can be safely validated. Otherwise document it as a separately qualifying future campaign; do not silently balloon scope.

## Workstream 8 — Validation ladder

Run the narrowest checks first, then the repository-supported broad gates.

Required minimum, adjusted only when the live repo proves a command has changed:

1. PowerShell parser/static sanity for changed scripts.
2. New drift-contract unit/fixture tests, including all adversarial cases.
3. `ffdoc_drift_tests` (or its deliberate replacement) directly and through CTest.
4. Any focused verifier/status/lifecycle tests affected by authority integration.
5. Strict OpenSpec validation for all changes.
6. Diagnostic/source-marker/policy checks that consume documentation or scripts.
7. `cmake --preset debug` + build through the supported Visual Studio environment.
8. Full Debug CTest.
9. `release-test` configure/build + full CTest.
10. Analyze configure/build and warning review.
11. Applicable ASAN protocol/IPC slice if shared build/policy wiring changed.
12. `pwsh ./verify/verify.ps1 doctor`.
13. `pwsh ./verify/verify.ps1 build` or the current official build gate.
14. Current project-status/release-readiness generation and schema validation.
15. `git diff --check` and complete diff review.
16. Push implementation and inspect the exact hosted Actions run for the implementation SHA.

Do not claim host-bound UIA, production signing, privileged MFT/USN, clean-host, guest/provider, or physical-display validation unless it was actually executed in the required environment. This campaign should normally not need those paths.

## Preserved behavior / non-negotiable constraints

- Preserve the three-process privileged/unprivileged architecture.
- Preserve the constrained LocalSystem service boundary and closed command protocol.
- Preserve symmetric Authenticode/image-path verification and fail-closed production Release behavior.
- Preserve degraded mode as a first-class permanent path.
- Preserve typed lifecycle schema-v2 evidence semantics and fail-closed status/release projection.
- Preserve raw unchecked tasks and genuine external blockers; do not cosmetically drive the raw count to zero.
- Preserve supported UI/engine/index/search/file-operation behavior; this is not a UI redesign or feature campaign.
- Preserve historical evidence files unless a current-facing pointer is objectively wrong.
- Do not merge/reset/cherry-pick PR #6 wholesale.
- Do not weaken tests, timeouts, security checks, or release criteria to obtain green output.
- Do not hard-code a specific AI model, harness, provider, or sub-agent mechanism into repository guidance.
- Do not introduce an online/network dependency for doc/truth validation.
- Do not read ignored verifier run/baseline/signing directories.
- Never force-push.

## Acceptance criteria

The campaign is complete only when all of the following are true:

1. A fresh OpenSpec change records the baseline, truth-surface inventory, authority ownership, concrete drift findings, negative-fixture matrix, implementation, and validation evidence.
2. Every current-facing guidance surface has been audited against the actual current source/config/test/verifier/lifecycle contract, not only recent diffs.
3. The known README verifier-verb drift is resolved and protected by an executable semantic check.
4. Stale current/active OpenSpec campaign wording is removed or replaced with a lifecycle-aware stable formulation and protected against regression.
5. Snapshot-publication guidance is reconciled with current publisher/reader behavior and protected by a non-brittle contract.
6. Signing/pin/degraded/production-readiness guidance consistently distinguishes implementation capability from externally validated production readiness.
7. The drift gate proves meaningful contradictions fail even when none of the original four banned phrases appear.
8. Adversarial fixtures prove the gate does not falsely reject clearly historical evidence.
9. Machine-derived facts have one authoritative owner or a mechanically derived/checkable relationship; the campaign does not create a second hand-maintained truth manifest.
10. CTest and hosted CI execute the semantic drift gate and propagate failures faithfully.
11. Lifecycle/status/release-readiness remain PASS/fail-closed exactly according to current schema-v2 semantics; no local work is hidden or fabricated.
12. Full Debug and release-test suites pass; Analyze is warning-clean; applicable verifier/policy/ASAN gates pass.
13. No introduced Critical/High regression remains open.
14. `progress.md`, `docs/agent/WORKSTATE.md`, the OpenSpec task/evidence ledger, lifecycle state, and this prompt are updated from final observed reality.
15. Implementation is committed and pushed to `main` without force; `HEAD`/`origin/main` parity is reconciled.
16. Hosted CI for the exact implementation SHA is inspected and recorded truthfully. Final documentation-only handoff SHA/CI may be derived live rather than made self-referential.

## Completion gate and handoff

Do not stop after making documentation read better. The point is to make future drift **detectable**.

At final handoff:

1. Re-run the focused semantic drift suite after all documentation edits.
2. Re-run the broad supported validation ladder.
3. Generate project status/release readiness and verify the lifecycle frontier has not been corrupted.
4. Review the complete diff for accidental product/security behavior changes.
5. Update the OpenSpec ledger/evidence, `progress.md`, `docs/agent/WORKSTATE.md`, `openspec/lifecycle.json`, and `.agent/EXECUTION_PROMPT.md` with observed final state.
6. Mark this prompt `Status: **COMPLETE**` only when all locally actionable requirements above are satisfied.
7. Commit with a detailed session report in the commit message: baseline, root cause, implementation, tests, any failed/retried gates, external blockers, and final delivery state.
8. Push to `main` without force.
9. Fetch/prune and verify remote parity.
10. Inspect hosted CI for the exact pushed implementation SHA. If later final-handoff CI cannot be embedded without self-reference, leave a precise live-derivation instruction rather than guessing.

If the live audit disproves the planner's premise and shows there is no semantic/current guidance drift after all, do not invent work. Record the evidence, close the prompt as not-applicable/complete, and stop. Otherwise, `/goal continue` should execute this campaign from the first incomplete requirement.

## Completion record (executor, 2026-08-26)

Executed to completion on `main` without force:

- Implementation `80b24ffa37185e90a02af798dd9245ce6894fb7b`; frontier
  reconciliation `72db280e9b41723a571c0f1367aa4ba8bb0be15b`. Campaign ledger:
  `openspec/changes/repository-truth-surface-and-doc-drift-convergence`
  (strict-valid; 72/72 with all changes).
- All acceptance criteria met: authority-derived semantic drift contracts
  (`tests/drift/DocTruth.psm1`) with fail-closed fences and lifecycle-aware
  anti-staleness; 19-case adversarial fixture matrix registered as CTest
  `ffdoc_truth_fixture_tests`; every planner example plus audit extras F7–F11
  reconciled (README verbs/presets/targets, stale current-campaign claims in
  README/CLAUDE/progress, snapshot slot-flip mechanics in AGENTS/CODE_INDEX
  plus two source comments); machine-derived facts keep one authoritative
  owner per `evidence/authority-map.md`.
- Validation: fixtures 19/19; both drift tests green via ctest locally and on
  fresh checkout; strict OpenSpec 72/72; status generation lifecycle PASS
  (24 raw unchecked / 0 active-local preserved); doctor PASS; official build
  gate 9/9 (debug/release-test + analyze); ASAN protocol/IPC 3/3;
  `git diff --check` clean.
- Hosted CI honestly recorded: run `32934249659` FAILED debug/release-test
  because wave 1 committed before the progress.md reconciliation — the new
  guard caught real committed-tree drift its own campaign had left behind.
  Fixed by `72db280`, verified green on a fresh clone pre-push, re-hosted run
  `32935961728` completed success for exact `72db280e9b41723a571c0f1367aa4ba8bb0be15b`.
- External prerequisites remain external and visible: production signer/pins,
  privileged MFT/USN validation, clean-host matrix, guest/provider,
  physical-display, foreground-owned interactive UIA. Draft PR #6 remains
  superseded evidence. No product/runtime/security behavior changed.
