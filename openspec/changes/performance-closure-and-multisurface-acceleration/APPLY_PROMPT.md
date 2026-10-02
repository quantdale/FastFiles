# Apply handoff — performance-closure-and-multisurface-acceleration

Use this file as the prompt for a new agent session in the FastFiles repository.

---

You are the apply agent for **`performance-closure-and-multisurface-acceleration`**. Repair the reviewed planning defects first, then execute the OpenSpec change through measured implementation, certification, and a reviewable **unmerged** pull request. Do not stop at another proposal or a benchmark interpretation correction.

Use `.agent/skills/openspec-apply-change/SKILL.md`. Invocation equivalent:

```text
/opsx:apply performance-closure-and-multisurface-acceleration
```

## 1. Read and reconcile before changing anything

Repository on the originating machine: `D:/Documents/tryPython/FastFiles`.

Read:

1. `AGENTS.md` and any applicable nested instructions.
2. `docs/agent/WORKSTATE.md` — the top checkpoint is the resume frontier; older checkpoints are history.
3. `.agent/skills/openspec-apply-change/SKILL.md`.
4. `openspec/changes/performance-closure-and-multisurface-acceleration/proposal.md`, `design.md`, every `specs/**/spec.md`, `tasks.md`, and `evidence/explore-findings.md`.
5. `openspec/lifecycle.json` and the predecessor `measured-performance-acceleration-and-exact-certification` task ledger and relevant evidence.
6. The originating campaign brief, `D:/Downloads/fastfiles.txt`, if available. Its concrete campaign requirements are preserved in this change and the corrections below. If it is unavailable, use these repo-local contracts; pause rather than invent an acceptance rule when a material ambiguity remains.
7. `build/proposal-review-06c192b.md`, if present. This is an ignored, local review report, not a portable dependency. All seven distinct findings are embedded below.

Announce the selected change, then run:

```powershell
git status --short
git fetch origin
git branch --show-current
git rev-parse HEAD
git rev-parse origin/main
git log --oneline origin/main..HEAD
git worktree list
openspec status --change performance-closure-and-multisurface-acceleration --json
openspec instructions apply --change performance-closure-and-multisurface-acceleration --json
```

Read every `contextFiles` path returned by the apply instructions. Derive current progress from `tasks.md`; do not confuse planning `isComplete` or CLI `ready` with semantic approval or completed implementation.

Historical context only:

- Planning was reviewed at `06c192b44412172802ac4c7627084094e135daa5`; there was one spec, 12 requirements, 23 scenarios, and 0/51 implementation tasks checked.
- The branch was `feat/performance-closure-and-multisurface-acceleration-20261001`, based on `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3`.
- The predecessor was merged as PR #12 but remained active with tasks 6.3 and 6.4 open.
- Hosted run `36786432149` failed six jobs before any steps. The annotations request returned HTTP 404; a billing cause was **not confirmed** for that run.
- Strict OpenSpec passed 91/91 and the lifecycle model passed. Neither pass resolves the semantic review defects.

Re-derive all Git, PR, lifecycle, and hosted facts. Preserve unrelated work. Do not restart repository-wide explore/propose or reuse the merged predecessor branch. If main has advanced, reconcile without a force-push or history rewrite; do not silently certify the old base as current.

## 2. Mandatory planning repair before production optimization

The reviewed proposal is **NOT APPLY-READY**. Repair these contracts consistently across proposal, spec, design, tasks, and the current WORKSTATE checkpoint where relevant. The references below identify the reviewed version; locate the current requirement/task by text if lines have moved. Preserve historical evidence and task traceability. Record the repair and re-review it before proceeding.

1. **Correctness is not a performance tradeoff.** The retention requirement in `specs/performance-closure-and-multisurface-acceleration/spec.md` (reviewed line 97) allows correctness and selective-search regressions when a larger win justifies them. Require no correctness regression and no material common-selective-search regression, unconditionally. Only a specifically justified, measured memory tradeoff may use the general exception; it must still satisfy the relevant surface's acceptance rule.
2. **Fix the reversed selective-search scenario.** The scenario at reviewed spec lines 139–144 says a regressing candidate is not retained “unless the tradeoff is explicitly rejected.” Remove that exception. A material selective-query regression rejects the search candidate, regardless of a broad-query speedup.
3. **Unverified local debt is not closed debt.** Reviewed spec lines 11–19 permit optimizing after a rerun or an unverified record, and design's Risks section (reviewed line 192) permits predecessor completion while it is explicitly unverified. Require current successful local prerequisites or an evidenced, genuinely external classification. A locally executable failed/unrun gate remains open and blocks production optimization. Investigation and necessary defect repair may proceed, but not a performance change that bypasses the gate. Mark the predecessor completed only after its debt is closed or honestly external, stale evidence is corrected, repository truth is reconciled, and current-state validation is recorded. Align task 2.7 and phase ordering. Do not mark tasks complete merely because their failed result was recorded.
4. **Keep 10% as a strong target, not an accidental hard floor.** Task 6.4 currently requires at least 10%, unlike the spec/design and originating brief. Retention requires a meaningful real-path gain clearly above the fresh same-binary noise floor, with correctness and regression protections. Ten percent remains the strong target. Do not strengthen the spec to match the accidental cutoff or use a historical ceiling as evidence.
5. **Outcome B requires an investigation, not a forced product edit.** If payload size accounts for most of the historical gain, investigate a separate wire-format/payload-reduction candidate and record a measured retention/rejection decision or a concrete blocker. Task 6.4 must not let this conditional investigation disappear as “optional.” Do not add an inline-string class for outcome B, and do not require retaining a format change without evidence.
6. **Prove bounded name-storage teardown as well as safe lifetime.** Extend the pooled/zero-copy requirement, design, and task 6.2 with bounded teardown and resource-retention guarantees consistent with existing ownership. Test stalled readers, repeated generation replacement, disconnect, teardown, and concurrent readers. Do not make shutdown wait indefinitely for a reader or retain every old generation in the engine. Preserve the current-mapping/independently-held-client-mapping contract in `src/engine/src/SnapshotPublisher.h`. This does not authorize claiming the separate privileged raw-I/O completion bound is solved.
7. **Prove equivalence for the actual final tip.** Task 11.4 currently precedes WORKSTATE and final-report edits (11.5 and 12.1). Reorder the final handoff so the last tracked documentation/task/report commit is included in the equivalence check. Any later tracked commit requires another check. Report implementation SHA, certified binary-embedded SHA, evidence SHA, repository tip SHA, executable-impacting digest, and verdict. Avoid a self-referential tracked report: record the final live tip/verdict in a generated artifact, PR body, or session response outside the checked commit.

Do not silently change unrelated product requirements. If a repair changes an existing capability contract, add the necessary explicit delta before implementing that behavior.

After the repairs:

```powershell
openspec validate performance-closure-and-multisurface-acceleration --strict --no-interactive --json
openspec validate --all --strict --no-interactive --json
```

Also run the lifecycle/project-status check and review the corrected acceptance language against all seven findings. The CLI's structural pass alone is insufficient. Commit the focused planning repair before starting production optimization.

## 3. Execute the repaired task ledger

Use `tasks.md` as the acceptance ledger. Mark a task complete only after its requirement is demonstrated, with an implementing symbol or evidence reference. Conditional rejection is a legitimate outcome when documented; a missing prerequisite is not. Keep one primary engineering frontier and checkpoint WORKSTATE at meaningful transitions and before long verification.

### A. Close certification debt and repair evidence

- Complete live reconciliation and the quiet-host prerequisite slice first.
- Stop only campaign-owned or demonstrably safe disposable processes. Never kill arbitrary user work to manufacture an idle host.
- Preserve the existing **30-second** assertion in `TestStopReturnsWithoutWaitingForTheSchedulerInterval`. No increase, skip, or lucky retry. Follow the bounded idle-run policy; an idle failure must be investigated before optimization.
- Obtain current Debug/release-test, Analyze, ASan, E2E, UIA core/full, visual, and focused many-tabs/focus evidence, with actual passed/failed/skipped totals and candidate identity. Do not inherit historical results.
- Label the short-name experiment **`CONFLATED ALLOCATION + PAYLOAD-SIZE EXPERIMENT`**. Preserve every historical measured number; never rewrite numbers to agree with a later report.
- Exercise the equivalence gate with documentation-only positive, compiled `.cpp`, header-only, unresolved quoted include, and CMake negative controls; include nested-header and added/deleted-input attacks in review. Record expected and observed verdicts and the failing input. Isolate synthetic commits in an owned temporary worktree, never in the pushed campaign history.
- A passing gate proves git-controlled executable-impacting source/build-input equivalence, **not reproducible binaries**. Respect the documented external compiler/SDK/environment boundary.

### B. Fresh baseline, noise, and allocation isolation

- Build a clean `release-test` baseline and verify embedded identity.
- Use the production-faithful move, established idle gate, one logical core, high priority, randomized interleaved order, warm-up, and sufficient process-level samples. Measure a same-binary control in that same session.
- Hold names, lengths, entry/directory/file counts, serialized bytes, and generation identity constant. A mismatch invalidates the allocation-only conclusion.
- Split export allocation from full-path allocation; add parse isolation if needed to identify the production side. Record stage medians, total latency, payload bytes, allocation count/bytes or explicitly `unmeasured`, fixture, source SHA, tree state, preset, and sampling method.
- Select outcome A/B/C from current data. No production name representation is justified by a gain inside noise. The historical 27.39% conflated result is not an allocation-only speedup or an expected retained gain.

### C. Name path, then search, then memory

- Outcome A: implement the smallest production change capturing the measured side. Prefer direct serialization from existing interned/generation-owned storage over a new inline-string abstraction.
- Outcome B: investigate payload reduction separately, with its own compatibility, correctness, lifetime, and retain/revert evidence. Outcome C: leave production name representation unchanged.
- Keep snapshot format version 3 unless a measured design requires a change. Incompatible peers must fail closed; no reinterpretation of a different layout.
- Prove Unicode, mixed case, pathological maximum-length names, copy/move/comparison, exact output equivalence, safe ownership, and bounded teardown. ASan and focused lifetime tests are mandatory for retained pooled views.
- Freshly benchmark selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation/replacement queries. Profile acquisition, normalized-field access, matching, result construction, sorting, top-N, and publication before choosing a search change.
- Decompose realistic resident projection memory and cross-layer duplication using measurements, not `sizeof` alone. Do not remove a useful index and call slower search a memory win.
- For every candidate: isolate, implement minimally, measure, retain or revert, and record the decision. Do not revive packed serialization, in-place row construction, or a fold-free matcher without genuinely new end-to-end evidence.
- Continue while a concrete, measured, material, locally executable high-value candidate remains. Do not stop merely because the first candidate was rejected, and do not invent speculative work to prolong the campaign.

### D. User-visible checks and flake investigation

Check navigation-to-ready, search-input-to-visible-results, tab-switch-to-ready, Home-to-ready, and large-directory-to-ready where practical. These are not brittle new wall-clock test gates. Separate an engine win from an unmeasured UI win.

Attempt the recorded `search-deep-result-navigation` failure (`alpha` not accepted; empty value on AutomationId 7101) with focused trials and an aggregate run. Distinguish readiness, stale caches, focus, text/value patterns, foreground state, control recreation, typing versus value-setting, and event synchronization. Fix only a reproduced harness/product cause; otherwise retain **`UNREPRODUCED / OBSERVED FLAKE`** and the historical failure.

## 4. Independent review and verification

This handoff requests the six fresh-context, read-only reviews in tasks 10.1–10.6:

- A: challenge noise, sampling, and payload identity.
- B: attack ownership, dangling views, stalled readers, replacement, disconnect, and teardown.
- C: challenge Unicode, mixed case, limits, sorting, and cancellation.
- D: expose latency/CPU regressions hidden by memory savings.
- E: adversarially attack executable-tree equivalence and its claim boundary.
- F: reconcile tasks, lifecycle, WORKSTATE, Git/PR/hosted facts, and benchmark records.

Follow the harness's delegation rules; use one writer per worktree and consume review results before final certification. Review output is evidence to verify, not automatic approval. Fix material reproduced findings. If governed child tooling fails, report the exact infrastructure blocker; do not silently switch protocols or impersonate independent review yourself.

Use the current repository's verification entry points and discovery, not guessed installed paths. Start with `pwsh ./verify/verify.ps1 doctor` if the shell lacks a configured VS toolchain. Run focused tests first, then the complete task 11 ladder on the clean final implementation candidate. Useful entry points include:

```powershell
cmake --build --preset debug --target <focused-test-target>
ctest --test-dir build/debug -R <registered-test-name> --output-on-failure
ctest --preset debug
ctest --preset release-test
cmake --preset analyze
cmake --build --preset analyze
pwsh ./verify/Invoke-CiParity.ps1
pwsh ./dev.ps1 test-e2e
```

Derive current ASan targets and qualification commands from `verify/Invoke-CiParity.ps1` and the task ledger; do not copy old totals. Run UIA core/full, visual catalog, strict OpenSpec, provenance, governance, project status, doc drift, hygiene, and unsigned release-candidate qualification as specified. Record skips/unavailable boundaries separately. Local parity is **`LOCAL-PARITY-NOT-HOSTED-CI`**, not hosted certification.

## 5. Final identities, commits, and PR

1. Commit the last executable-impacting edit, including relevant harness/tests, and establish `IMPLEMENTATION_SHA`. Cleanly rebuild and certify binaries embedding that identity. A later executable-impacting change invalidates this certification and must be rebuilt/retested.
2. Commit remaining evidence, task updates, final report, and WORKSTATE. Preserve historical results and record retained/reverted decisions.
3. Run the corrected equivalence gate from `IMPLEMENTATION_SHA` to the actual final evidence/repository tip. If a subsequent commit occurs — even a checkbox, report, or WORKSTATE edit — run it again. Do not write another tracked annotation merely to embed the last invocation; keep its final live verdict outside the checked tip.
4. Check scope, `git diff --check`, and clean status. Stage only session-owned files, make coherent commits, and push the feature branch without force. **Do not push the campaign to main, merge, archive it automatically, or delete its branch.** The explicit campaign no-merge requirement overrides the generic main-handoff convention.
5. Open/update one reviewable PR at the handoff phase. Set `MERGE_ELIGIBLE=YES` or `NO` with exact reasons; this statement is not permission to merge.
6. Inspect hosted CI once for the final pushed tip. If jobs never execute, record `HOSTED_CI=BLOCKED_EXTERNAL` and the observable cause/evidence; do not assume an unavailable annotation says billing. Do not rerun a known-unstartable workflow or mutate billing/permissions/protection. If executed jobs fail for a repository reason, fix that failure and repeat affected verification on the new candidate.

The final report must contain A–M: repository truth; current certification counts; flake result; equivalence controls and claim boundary; allocation isolation; name decision; search decision; memory decision; user-visible effect; reverted ideas; all SHA identities/digest/verdict; hosted CI; and remaining work. Include the actual pushed SHA, PR URL, reviewer dispositions, before/after/noise measurements, exact commands, and honest blockers.

## 6. Non-negotiable scope and stop conditions

- Preserve the three-process security architecture, degraded/unprivileged operation, closed privileged command surface, parser bounds, signed-peer authentication, and DLL hardening. Read the foundation design before changing IPC/security/process boundaries. No generic open-path primitive and no file-content forwarding.
- No certificate provisioning, trust-store writes, Administrator requirement, production signing-pin changes, privileged trust opt-in, or elevated MFT/USN validation in this campaign. Use portable unsigned development tests. Production Release remains fail-closed without authorized pins.
- Never read/glob `verify/runs/`, `verify/baselines/`, `verify/.signing/`, or auxiliary `.kilo` worktrees. Leave the existing auxiliary worktree untouched.
- Leave production signing, clean-host install, raw-volume completion bounds, physical display matrix, and guest/provider requirements external without evidence. Keep `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 unchecked without their required evidence; do not close them by supersession.
- Do not weaken assertions, hide failed runs, invent allocation counts, fabricate closure, or call a documentation/benchmark-only change a product speedup.
- If a genuinely external prerequisite or unresolved material design ambiguity prevents safe progress, checkpoint the exact command/evidence/blocker and next action, keep tasks honestly open, commit only owned changes, and report the pause. Do not bypass it merely to finish the task list.

Successful handoff means measured retained improvements **or** defensible rejection of every concrete high-value candidate, accurate certification/lifecycle truth, a mechanically established final-tip relationship, a clean committed feature branch, and an explicitly unmerged reviewable PR. Begin with live reconciliation and the seven planning repairs above.
