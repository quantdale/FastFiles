# NEXT CAMPAIGN — FASTFILES

Status: **ACTIVE**
Planned-From: `c7088aa2b6d58babe66553bba3e9f2cb52e803af`
Planned-At: `2026-08-26T17:33:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **Committed-Tree Validation Provenance + Clean-Checkout Parity Convergence**

## Mission

Make FastFiles validation evidence provably refer to the exact repository tree that is being handed off, qualified, or used for release-readiness decisions.

The immediately preceding `repository-truth-surface-and-doc-drift-convergence` campaign is complete. Current `main` reports lifecycle `PASS`, **24 raw unchecked tasks and 0 active-local tasks**, a green final-handoff hosted run, and only genuine external/host-bound production signing, privileged MFT/USN, provider/guest, clean-host, physical-display, foreground-owned UIA, and related evidence gates. There is no justified product-feature campaign to manufacture from that state.

The last campaign exposed a concrete remaining local hardening gap. Its first hosted implementation run failed because local validation had observed the corrected **dirty working tree**, while hosted CI validated the actual **committed tree** where `progress.md` was still stale. The newly strengthened doc-truth guard correctly detected the committed-tree defect in CI, and the corrected commit was then verified from a fresh checkout. That incident proves that a locally green validation matrix can still be truthfully green for one filesystem state while being cited as evidence for a different Git commit.

Close that class of false-green handoff generally. Validation results used as durable evidence must carry exact repository-state provenance and must not silently qualify a commit when the checks actually ran against staged, unstaged, untracked, or subsequently mutated source state. Development-time dirty-tree validation may remain useful, but it must be unmistakably non-qualifying unless the exact content identity is preserved and consumed as such.

Do this without destructive worktree cleanup, without stashing or discarding user work, without adding a network dependency to ordinary validation, without weakening the existing verifier result model, and without relabeling external release prerequisites as local work.

## Planner findings that must be independently revalidated

Treat these as starting evidence, not unquestionable truth. Reconcile against live `main` before editing.

1. Current remote `main` is `c7088aa2b6d58babe66553bba3e9f2cb52e803af` (`repository-truth: record final truth-convergence handoff`).
2. Hosted Actions run `32939084333` completed successfully for exact SHA `c7088aa2b6d58babe66553bba3e9f2cb52e803af`.
3. `.agent/EXECUTION_PROMPT.md` is COMPLETE for `repository-truth-surface-and-doc-drift-convergence` before this planner update.
4. `progress.md`, `docs/agent/WORKSTATE.md`, and `openspec/lifecycle.json` report the previous campaign complete, lifecycle integrity PASS, 24 raw unchecked tasks, 0 active-local tasks, and external production/interactive boundaries preserved.
5. The previous campaign's first hosted run `32934249659` failed `ffdoc_drift_tests` because the implementation wave was committed before `progress.md` was reconciled. The local gate had passed against the corrected dirty worktree; CI evaluated the committed tree and caught the stale current-campaign claim. The corrected commit `72db280e9b41723a571c0f1367aa4ba8bb0be15b` then passed hosted run `32935961728` and fresh-checkout doc-truth validation.
6. This was not merely a documentation typo. It demonstrated an evidence-identity problem: the repository can currently produce a local PASS that is valid for the working directory contents but is later narrated as if it certified the commit that was pushed.
7. `AGENTS.md` requires session-owned changes to be committed, the working tree to be clean, remote parity to be verified, and hosted CI for the pushed SHA to be inspected before successful handoff. That is strong procedural guidance, but the planner found no evidence yet that every qualifying local verifier result is mechanically bound to the exact Git tree/commit it claims to support. Revalidate before changing anything.
8. The runtime-verifier hardening campaigns already introduced `runId`/`invocationId`, schema-validated result envelopes, exact capability-result composition, and explicit result selection. Reuse that identity model where appropriate; do not create a competing result system.
9. The lifecycle evidence-integrity campaign already introduced typed Git-commit and hosted-run evidence with offline commit reachability checks and full-history CI checkout. Preserve that model and extend exact-tree qualification only where a real gap remains.
10. Current repository truth checks now derive facts from executable seams and have adversarial fixtures. Do not weaken or duplicate those checks. The new campaign is about **which repository state was validated**, not re-solving semantic doc drift.
11. Ordinary development intentionally permits generated/ignored build, verifier-run, baseline, and signing directories. A clean-source qualification contract must distinguish tracked source dirtiness from ignored generated state and must not read ignored evidence directories merely to compute source provenance.
12. The repository currently has no open issues.
13. Draft PR #6 (`stabilization/ship-ready-2026-08-23`) remains open but superseded by later mainline work. Do not merge/reset/cherry-pick it wholesale. It is historical comparison evidence only unless a current-main defect is independently reproduced.
14. Existing production signer/pins, privileged MFT/USN execution, privileged trust-store integration, clean-host release matrix, guest/provider validation, and physical/foreground-owned interactive UI evidence remain external or opt-in. Do not absorb them into this campaign.
15. If live reconciliation proves the verifier already has a complete exact-tree provenance contract that prevents the observed false-green class across local qualification, release-readiness, and handoff evidence, do not invent replacement machinery. Record the proof and STOP.

## Mandatory reconnaissance before edits

Do not implement from the planner snapshot without live reconciliation.

1. `git fetch --prune origin`.
2. Record current branch, local `HEAD`, `origin/main`, ahead/behind counts, worktree state, index state, untracked state, and recent commits.
3. Start from the actual current `main`; reconcile safely without force and without discarding valid work.
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
   - root `CMakeLists.txt` / `CMakePresets.json`
   - `verify/verify.ps1`
   - verifier core/result/schema modules
   - `verify/status/**`
   - `verify/release/**`
   - `verify/README.md`
   - `verify/intake.ps1`
   - `tests/drift/**`
   - `openspec/lifecycle.json`
   - `verify/schemas/openspec-lifecycle.schema.json`
   - project-status and release-readiness schemas
   - the last three completed hardening campaign proposal/design/tasks/evidence directories.
5. Inspect complete diffs and handoff evidence for at least:
   - `f2d55716f82f56eb332805da2c26cc560cd98f32`
   - `2b600eb9a8d1f8d4e20168653da50afcddc97717`
   - `cd8b0b1bd60fbb2af25944f172eca1f5b1614684`
   - `09bac46a5371ecaad58b7c38ab044deb2c5d8163`
   - `80b24ffa37185e90a02af798dd9245ce6894fb7b`
   - `72db280e9b41723a571c0f1367aa4ba8bb0be15b`
   - `c7088aa2b6d58babe66553bba3e9f2cb52e803af`.
6. Inspect open PRs/issues. PR #6 is historical/superseded evidence only unless current-main behavior independently demonstrates a remaining defect.
7. Generate current `status`, `release-readiness`, `doctor`, and any fast verifier metadata needed to understand how repository identity is represented today.
8. Trace every path by which a PASS becomes durable evidence: direct CTest runs, `verify.ps1` verbs, project-status generation, release-readiness/candidate generation, CI job output, OpenSpec evidence records, workstate handoffs, and final session reporting.
9. Search for existing Git/repository provenance fields such as HEAD SHA, tree SHA, branch, dirty/clean flags, source-root identity, commit reachability, run input hashes, candidate SHA, and result selection. Build an authority map before adding anything.
10. Reproduce the false-green class in a hermetic fixture or temporary Git repository before implementing the fix: one state should pass locally, while the committed state remains stale/failing. Do not mutate real user work merely to demonstrate it.
11. Do not read or glob ignored `verify/runs/`, `verify/baselines/`, or `verify/.signing/` as input to the source-truth model.
12. Use repository-native tools and the available environment. Do not assume a specific AI model, coding harness, provider, or sub-agent implementation.

## Fresh campaign ledger

Create a new OpenSpec change named equivalently to:

`committed-tree-validation-provenance-and-clean-checkout-parity`

Include at least:

- `.openspec.yaml`
- `proposal.md`
- `design.md`
- `tasks.md`
- an appropriate spec delta/new spec for validation provenance and qualification semantics
- `evidence/baseline.md`
- `evidence/validation-flow-inventory.md`
- `evidence/provenance-authority-map.md`
- `evidence/false-green-reproduction.md`
- `evidence/fixture-matrix.md`
- `evidence/implementation-and-validation.md`

The baseline must pin exact pre-change Git/status/CI facts and must distinguish planner observations from independently reproduced findings.

## Workstream 1 — Inventory every validation-to-evidence path

Build a complete map of how repository checks become claims.

For each relevant path record:

1. the command/capability that executes;
2. the source files/state it actually reads;
3. whether it can run in a dirty worktree;
4. the Git identity captured today, if any;
5. whether the result is development-only or can influence qualification/release readiness;
6. how the result is persisted or referenced;
7. whether a later commit can be incorrectly described as having passed that result;
8. whether the source tree can mutate during the run without invalidating the result.

At minimum cover direct CTest, official verifier build/gate flows, status/release-readiness/release-candidate, doc/policy/OpenSpec gates, CI jobs, and handoff evidence.

Do not assume that every local PASS must require a clean tree. Fast developer feedback is useful. The requirement is that **qualification semantics are explicit and exact**.

## Workstream 2 — Define one repository-state provenance contract

Design before implementation and reuse existing verifier/result identity primitives.

A qualifying validation record should be able to establish, where applicable:

- repository identity;
- exact `HEAD` commit SHA;
- exact commit tree SHA;
- whether tracked files differed from HEAD at validation start;
- staged vs. unstaged tracked changes;
- relevant untracked-source state if repository policy treats it as executable input;
- whether the source state changed while the validation was running;
- selected qualification mode (`development` vs. clean/exact-tree qualification or equivalent);
- candidate/release SHA when a release projection consumes the result;
- deterministic, path-safe provenance diagnostics.

Avoid storing raw diffs or private absolute paths merely to prove dirtiness. Prefer stable identities/digests and repository-relative diagnostics.

Ignored generated outputs must not make a source tree non-qualifying merely because build/run artifacts exist. Conversely, do not let an executable untracked source/config file silently escape the provenance model if it can affect the result.

If existing runtime result envelopes already have the right extension seam, extend them. If a smaller shared Git-state module is appropriate, give it one authoritative owner and test it directly. Do not introduce two independent Git-provenance representations.

## Workstream 3 — Separate development validation from qualifying validation

Make the semantic boundary executable.

Required behavior:

- development-time checks may run against a dirty tree when useful;
- any such result must be marked non-qualifying for exact-commit handoff/release evidence unless the system explicitly represents the exact dirty content identity and consumers understand it;
- release-readiness / release-candidate logic must not accept a PASS produced for a different commit/tree as evidence for the current candidate;
- final handoff helpers must fail closed or clearly refuse to claim exact-commit local qualification when tracked source state changed after validation;
- no tool may automatically `reset`, `clean`, `checkout`, `stash`, or otherwise destroy/hide user work to satisfy the contract.

A simple "working tree clean now" check at the very end is insufficient if the tree could have changed between validation and handoff. Bind the result to what was actually observed.

## Workstream 4 — Add clean committed-tree parity validation

Provide a deterministic way to prove the committed tree itself passes the repository's fast qualification-sensitive gates.

Prefer an offline mechanism based on the local repository, such as a temporary detached Git worktree or another content-addressed materialization of the exact commit. Do not require a network clone for ordinary local validation.

The committed-tree parity slice must at minimum cover fast gates that can produce the same class of discrepancy seen in the previous campaign:

- semantic documentation truth/drift checks;
- OpenSpec/lifecycle validation;
- source-marker/diagnostic/policy checks;
- project-status / release-readiness structural generation where practical;
- other fast repository-state-sensitive checks found by the audit.

Decide from measured cost whether the full build/CTest matrix should also run in the clean materialized tree or whether exact-tree provenance plus hosted CI already provides the stronger full-matrix guarantee. Record the tradeoff; do not duplicate multi-minute builds without evidence that it closes a real gap.

Temporary worktrees/materializations must be uniquely named, cleaned up safely when owned by the current run, and never delete a pre-existing path or user worktree.

## Workstream 5 — Prevent stale-result replay and mid-run mutation

Add fail-closed checks so old green evidence cannot be replayed against new source state.

At minimum handle:

- result from commit A consumed while HEAD/candidate is commit B;
- same branch name but different SHA;
- result created before staged/unstaged tracked changes;
- tracked source modified while a qualifying run is in progress;
- result file copied/reused after source mutation;
- detached HEAD;
- shallow/full-history differences where relevant;
- ignored generated artifacts present;
- untracked executable/config input where applicable;
- missing Git metadata / non-repository invocation;
- path redaction and deterministic diagnostics.

Do not weaken exact hosted-run evidence. Hosted CI results remain bound to their `head_sha`; local provenance should converge with that model, not compete with it.

## Workstream 6 — Integrate provenance into status and release semantics

Audit every consumer before wiring changes.

Where local validation evidence contributes to status or qualification:

- expose whether the result is exact-tree qualifying or development-only;
- bind candidate decisions to the expected commit/tree identity;
- preserve lifecycle integrity and blocked/external buckets;
- fail closed on contradictory or missing provenance when a production-ready claim would otherwise depend on it;
- do not turn ordinary status generation into a network call;
- preserve typed lifecycle evidence and existing exact hosted-run semantics.

Do not make external signer/provider/clean-host/interactive gates disappear merely because the local commit is now exactly validated.

## Workstream 7 — Add adversarial Git-state fixtures

The provenance contract must be proven with deterministic tests, preferably using hermetic temporary Git repositories/worktrees.

Cover at minimum:

- clean HEAD exact-tree PASS;
- staged tracked modification;
- unstaged tracked modification;
- tracked deletion;
- untracked source/config file that matters to policy;
- ignored build/run output that must not invalidate source cleanliness;
- validation result from prior commit;
- HEAD changed after result creation;
- mutation during a qualifying run;
- detached HEAD;
- missing repository metadata;
- temporary clean-worktree creation and owned cleanup;
- pre-existing destination collision must fail safely;
- deterministic ordering/reason codes;
- repository-relative/path-redacted diagnostics;
- the historical dirty-worktree/committed-tree false-green reproduction.

Tests must prove both false-negative resistance and false-positive discipline. Do not replace the current procedural handoff rule with a brittle string check.

## Workstream 8 — CI and hosted parity

Audit `.github/workflows/ci.yml` and exact-SHA result flow.

Required outcomes where applicable:

- checkout provenance is explicit and compatible with lifecycle Git-evidence validation;
- CI records/exports the exact commit/tree identity used for qualification;
- repository-state-sensitive fast gates run against the checked-out committed tree;
- any generated qualification summary cannot claim a different SHA/tree;
- no hidden dependence on branch names or "latest" artifacts;
- no online lookup is added to offline local status generation.

Do not add redundant CI jobs merely to inflate coverage. Reuse existing jobs/matrices when possible.

## Workstream 9 — Reconcile guidance and autonomous handoff rules

Update only current-facing guidance affected by the new semantics.

At minimum explain:

- development PASS vs. exact-tree qualifying PASS;
- what Git identity is recorded;
- when clean committed-tree parity is required;
- how final handoff proves the pushed SHA rather than the pre-commit dirty tree;
- why ignored build artifacts do not equal tracked-source dirtiness;
- why user work is never auto-reset/stashed/cleaned;
- how hosted CI remains exact-SHA evidence.

Keep current doc-truth fences and authority ownership intact. If new machine-derived claims are added to current guidance, extend the semantic truth contract where appropriate rather than creating unguarded prose.

## Required validation ladder

Run the narrowest checks first, then broaden. Exact commands may evolve after reconnaissance, but the final evidence must cover the repository's authoritative equivalents of:

1. New Git/provenance unit/PowerShell fixture suite.
2. Historical false-green reproduction now rejected or classified non-qualifying.
3. `ffdoc_drift_tests` and `ffdoc_truth_fixture_tests`.
4. Focused project-status/release-readiness tests.
5. Strict OpenSpec validation.
6. `pwsh ./verify/verify.ps1 status`.
7. `pwsh ./verify/verify.ps1 doctor`.
8. Official verifier build gate.
9. Debug configure/build + full CTest.
10. Release-test configure/build + full CTest.
11. Analyze configure/build warning-clean.
12. Required ASAN protocol/IPC slice.
13. Source-marker, diagnostic-policy, documentation-drift, lifecycle/status, and other applicable policy gates.
14. Clean committed-tree parity slice against the exact implementation commit/tree.
15. `git diff --check` and complete final diff review.

Do **not** rerun host-bound full live UIA, production signer/pin provisioning, privileged MFT/USN, provider/guest, clean-host, or physical-display validation merely because this campaign exists. Those boundaries remain external unless this implementation directly changes those paths, which it should not.

If broad validation exposes a deterministic Critical/High regression caused by this campaign, fix it before handoff. If it exposes a pre-existing unrelated defect, classify it with evidence and decide whether it is a genuine next frontier rather than silently expanding scope.

## Non-negotiable invariants

- Preserve the three-process privileged/unprivileged architecture.
- Preserve closed protocol and privileged security invariants.
- Preserve symmetric Authenticode/image-path verification and fail-closed production Release behavior.
- Preserve degraded mode as a first-class permanent path.
- Preserve schema-v2 typed lifecycle evidence and fail-closed status/release projection.
- Preserve exact hosted-run SHA binding.
- Preserve verifier run/result identity semantics unless extending them compatibly.
- Preserve raw unchecked tasks and genuine external blockers; do not cosmetically drive the raw count to zero.
- Preserve current product/UI behavior; this is not a feature or redesign campaign.
- Do not merge/reset/cherry-pick PR #6 wholesale.
- Do not weaken tests, timeouts, security checks, release criteria, or provenance requirements to obtain green output.
- Do not force-push.
- Do not auto-reset, auto-clean, auto-stash, or discard user work.
- Do not hard-code a specific AI model, provider, or harness into repository behavior.
- Do not introduce a network dependency for ordinary local qualification/provenance checks.
- Do not read ignored verifier run/baseline/signing directories as source-truth inputs.

## Git and delivery requirements

- Work on current `main` unless repository rules require a safer temporary branch; final durable state must reconcile with `origin/main` without force.
- Commit logical implementation/evidence waves with descriptive messages.
- Keep generated build trees, verifier runs/baselines/signing material, temporary worktrees, databases, WAL/SHM files, and host artifacts out of commits.
- Before final push, fetch/prune and reconcile safely with remote changes.
- Run the clean committed-tree parity slice against the exact commit intended for handoff, not merely against the preceding dirty worktree.
- Push to `main` without force.
- Verify local HEAD and `origin/main` parity.
- Inspect hosted CI for the exact pushed implementation SHA.
- Update `docs/agent/WORKSTATE.md`, `progress.md`, lifecycle/OpenSpec evidence, and this prompt from actual final Git/validation reality.
- Do not create a self-referential tracked edit merely to embed the CI result of that same edit; derive later handoff-SHA CI live when necessary.
- Mark this prompt `Status: **COMPLETE**` only after implementation, adversarial fixture proof, broad validation, exact-tree qualification, durable evidence, and Git handoff are done.
- The final commit message must be a detailed session report: baseline, reproduced false-green class, root cause, design/provenance choices, changed contracts, regression matrix, exact-tree validation, hosted CI, external boundaries, final state, and next-frontier/STOP result.

## Executor continuation contract

Use `/goal continue` or the repository's equivalent autonomous continuation command.

At every resume:

1. Fetch/prune and derive live `HEAD`, `origin/main`, worktree/index state, and current prompt/lifecycle state.
2. Revalidate the planner premise before implementation.
3. Continue from the first incomplete acceptance requirement; do not restart completed work merely because a new session began.
4. Keep evidence exact and repository-relative.
5. Do not claim a commit is qualified by a result whose provenance refers to different source content.
6. If the premise is disproven and existing mechanisms already close the class comprehensively, record the proof, mark the campaign not-applicable/complete, and STOP rather than manufacture churn.

## Completion gate

This campaign is complete only when all of the following are true:

1. The historical dirty-worktree/committed-tree false-green class has a deterministic reproduction and a deterministic prevention/classification test.
2. Qualifying local validation is bound to exact repository-state provenance.
3. Dirty-tree development results cannot silently qualify a different commit.
4. Stale-result replay and mid-run source mutation are handled fail-closed where qualification depends on the result.
5. Fast committed-tree parity validation exists for repository-state-sensitive gates, or the executor records stronger existing exact-tree evidence that makes an added mechanism unnecessary.
6. Status/release consumers reject provenance contradictions rather than infer success.
7. Adversarial Git-state fixtures pass.
8. Existing doc-truth, lifecycle, verifier, product, security, build, CTest, Analyze, and required ASAN gates remain green.
9. External signing/privileged/provider/clean-host/interactive boundaries remain truthful and untouched.
10. OpenSpec/workstate/progress/lifecycle records reflect actual final state.
11. The exact implementation commit is pushed without force and clean committed-tree validation has been run for that commit/tree.
12. Hosted CI for the exact pushed implementation SHA is inspected and reported when available.
13. This prompt is marked COMPLETE with an evidence-backed completion record.

If the live audit disproves the premise and shows that exact-tree provenance, stale-result rejection, and qualifying clean-tree semantics already comprehensively prevent this failure class, do not create a second mechanism. Record the evidence, mark the prompt complete/not-applicable, and STOP.
