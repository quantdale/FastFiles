# Finalize FastFiles for main readiness

Use this as the prompt for a new engineering session in `D:/Documents/tryPython/FastFiles`.

---

You are the finalization agent for **`performance-closure-and-multisurface-acceleration`**. Your sole objective is to make this branch **genuinely ready for a safe, policy-compliant update to `main`**: finish the remaining mandatory local acceptance, resolve every material repository-owned issue found in the branch's implementation, evidence, reviews, and verification, and deliver a clean, certified, pushed feature branch with an accurate readiness verdict.

This is execution, not another proposal or a documentation-only claim of completion. Begin with task **8.1**, then carry its consequences through review, certification, evidence reconciliation, and the final handoff. Do not stop merely after producing a memory table or ticking the last checkbox.

**Preparing for main is not permission to update main.** Preserve the campaign's explicit no-self-merge boundary (design D10 and the spec): keep PR #13 unmerged, do not push to `main`, force-push, rewrite history, or delete branches. This boundary overrides the generic main-handoff convention in `AGENTS.md`. Report readiness; a later authorized main update is a separate action. This prompt does not activate Goal mode or inherit an earlier goal ID.

## 1. Resume the real frontier

Read `AGENTS.md`, applicable nested instructions, `docs/agent/WORKSTATE.md`, `AUTONOMOUS.md`, `.agent/skills/openspec-apply-change/SKILL.md`, and this change's proposal, design, full spec, tasks, final report, and `evidence/projection-memory-decomposition.md`. Consult `openspec/lifecycle.json`. Use the OpenSpec apply workflow and read every returned `contextFiles` path.

```powershell
git status --short --branch
git fetch origin
git branch --show-current
git rev-parse HEAD
git rev-parse origin/main
git log --oneline origin/main..HEAD
git worktree list
openspec status --change performance-closure-and-multisurface-acceleration --json
openspec instructions apply --change performance-closure-and-multisurface-acceleration --json
gh pr view 13 --json url,state,headRefName,headRefOid,baseRefName,mergeable,mergeStateStatus,reviewDecision,statusCheckRollup,body
```

Checkpoint facts, **not permanent live truth**:

- Reviewed documentation tip: `0399a8efaba97ce52fa0442dab50eba5a891c9e1`; feature branch `feat/performance-closure-and-multisurface-acceleration-20261001`; main baseline `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3`.
- Certified implementation/binary identity: `ae1d03f0bb90b109cd781d07223fd69ec0f8ec7a`. All five engine preset identities were verified. The last recorded gate to `0399a8e` passed with digest `c9c660e85d7c744b90146c53424ee22c9876e5cdfc96a71cddf3e571871df021`, 389 paths / 309 translation units / 33 build inputs. Later tracked commits require a new final-tip gate.
- Task ledger **50/51**, task **8.1 open**, lifecycle `active`, PR #13 open, `MERGE_ELIGIBLE=NO`.
- Latest documentation review: **Standards 0, Spec 0**. That closes the documentation repairs, not memory acceptance or campaign completion. Optional local report: `build/fourth-review-0399a8e.md`; do not require ignored review artifacts to exist on another machine.
- The root-admission correctness repair and eight-case regression matrix are implemented. The current full ladder was recorded at `ae1d03f`: parity 11/0/0, Debug and release-test 127/127, Analyze clean, authoritative ASan 4/4 plus focused tests, E2E 8/0/0, UIA core 16/0/0, UIA full 23/6/0, visual 20/20, many-tabs 1/1. These are prior results, not tests freshly run by you.
- Hosted runs `37188049760` and `37188045876` each failed six jobs before any steps: `BLOCKED_EXTERNAL`. No billing cause was established for those runs.

Reconcile changes made since those checkpoints. The old `APPLY_PROMPT.md` is historical: its seven planning repairs and implementation phases have already run. Do not restart them or repeat the certification ladder merely to reproduce an unchanged historical record. Do not assume task 8.1 remains the only issue after new implementation/review.

Keep exactly one primary engineering frontier in WORKSTATE. Preserve unrelated work. If main advanced, integrate the actual base into the feature branch without rewriting history, resolve conflicts deliberately, and revalidate the resulting candidate. Never overwrite unknown local commits or inspect auxiliary worktrees to reconcile it.

## 2. Complete task 8.1 with real decomposition

The existing 1M-row record reports about **520.5 B/row process-private delta**, an estimator of **404.4 B/row**, a **104-byte row floor**, and a **116.1 MB / roughly 22% unseparated residual**. It does not contain the required per-category/capacity and allocation/repeat-rebuild proof. Reopening the task was honest; it was not completion. The owning export probe also did not separately weigh the retained pooled export.

Read the current `Projection`, `NamePool`, filename-index, rebuild/export, and memory-benchmark code before choosing instrumentation. Prefer a small benchmark-scoped allocation/capacity probe or a narrow read-only diagnostic seam. Do not introduce a production representation change merely to obtain measurements, fake category totals from `sizeof`, or weaken encapsulation with private-access macros.

Produce fresh, reproducible machine-readable results and a concise interpretation on the actual candidate at **200k and 1M rows**. Use the real rebuild path and documented fixtures. Record source and embedded binary identity, clean/dirty state, preset, commands, fixture properties, process baselines, repetitions, units, instrumentation domain, and limitations.

Account for each required contributor, avoiding double-counting:

- Live projection rows, row-vector capacity/slack, identifiers, and measured layout/padding.
- Original and normalized names, interned storage, arena capacity/slack and metadata; prove absence or shared storage where a separate category is not allocated.
- Identifier and parent/child maps: elements, buckets/nodes, contained vector capacities, and allocation overhead.
- Filename secondary indexes: posting payloads, vector capacities, bucket/node overhead, and non-ASCII structures.
- LRU/eviction metadata, free lists, path structures, and other actually allocated ownership structures.
- Allocator requested/live/peak bytes and allocation counts when instrumented; distinguish alignment/rounding/heap retention from requested payload.

Separate **process private bytes**, **working set**, **shared mapped/committed bytes**, **live allocated bytes**, and estimator sums. Do not call private bytes a resident-working-set measurement. Reconcile measured totals with category accounting and disclose any residual and its measurement boundary. Do not invent allocator counts or attribute a residual to fragmentation, a leak, or SQLite without evidence. A small bounded residual is not required to become zero, but an unexplained large aggregate must not masquerade as the required decomposition.

Run repeated same-scale rebuilds and clear/release/teardown cycles, not just one post-rebuild snapshot. Show whether live allocations/capacities and process memory plateau, return to a justified baseline, or keep growing; report the cycle counts and high-water marks. Distinguish allocator-retained pages from still-owned allocations. If growth is real, reproduce and fix its cause, then repeat the proof.

Update the cross-layer duplication classification from current source. Weigh retained pooled export/publication separately from the owning reference path; distinguish durable SQLite, hot projection, name storage, filename index, transient export, shared generation mapping, UI parse ownership, and query results. Do not infer UI measurements from an engine-only probe or relabel the old owning-map numbers as pooled measurements.

Accept 8.1 only when the scoped categories, allocation/working-set accounting, and repeat-rebuild evidence actually support it. Preserve old numbers as historical; add new evidence rather than rewriting them into agreement. Capture portable commands/data needed to reproduce the result, not only a path to ignored scratch output.

## 3. Resolve what the evidence reveals

Reassess task **8.3** using the completed decomposition. The old rejection is scoped, not proof that no useful memory candidate exists.

- If no concrete material safe candidate remains, record a defensible rejection with measured contributors and tradeoffs. A rejection can satisfy the campaign; a forced optimization cannot.
- If the evidence exposes a concrete material locally executable candidate or defect, isolate it, implement the smallest correction, test and measure it, then retain or revert it. Do not leave a known material problem open merely to declare readiness.
- Ten percent remains a **strong target, not a mandatory cutoff**. Use realistic scale and the relevant noise/control protocol. A `sizeof` saving alone is not a measured resident-memory improvement.
- Do not remove the useful filename accelerator and count slower search as a win. Retention must satisfy the spec's search, navigation, indexing, startup, reconciliation, and correctness protections. Correctness and material common-selective-search regressions are not tradeable for performance.
- Any representation/ownership change requires focused correctness, Unicode/mixed-case, capped sorting/limits, cancellation/replacement, copy/move, lifetime, stalled-reader, disconnect, concurrent-reader, and bounded-teardown coverage as applicable. Keep the full root regression matrix: filename/relevance × both directions × caps 1 and 2, hot/durable ordered equivalence, and the winning `Z:\` case.

Resolve every material repository-owned finding discovered in this finalization. Use red-first regressions for reproduced defects and verify the correction, not merely the reviewer's suggested patch. Keep speculative and unrelated improvements out of this campaign.

## 4. Independent review before final certification

This prompt explicitly requests delegated, fresh-context, read-only reviews with separate **Standards** and **Spec** lanes. Review the complete candidate delta against the current `origin/main`, with extra scrutiny of new memory accounting, repeat-rebuild evidence, pooled ownership, the capped-root repair, and final provenance. Historical documentation-only approval does not approve the full implementation branch.

Use the available code-review/delegation skills and governed subagent protocol. Discover executable agents first; compose the authorized lanes inside one top-level asynchronous workflow; bind durable outputs through the workflow's output options. Keep one writer per worktree and consume both lane results before accepting the candidate. Do not silently replace a failed independent lane with a parent self-review or switch runners/protocols. Report infrastructure failures and use the supported resume/owner path.

Require severity, exact file/line, governing requirement, trigger/consequence, and proposed correction for actionable findings. Preserve both axes in the result. Have the reviewers challenge whether all memory categories and allocator residuals are genuinely accounted for, not simply echo checked boxes. Reproduce material findings, repair them, and obtain review of the resulting delta; do not dismiss them solely because tests passed. Record review coverage and residual risks. No independent approval may be fabricated when a lane is unavailable.

## 5. Certify the final executable candidate, once stable

Use the existing repository verification entry points and discovered Visual Studio toolchain. Run focused tests first, then the required clean-candidate ladder **after** implementation/review repairs stabilize. Do not weaken `/W4 /WX`, Analyze, assertions, fuzz tests, or qualification gates to achieve green.

If any executable-impacting input changes — including benchmarks, tests, headers, CMake inputs, or verification helpers — establish a new committed **`IMPLEMENTATION_SHA`**, rebuild identity-bearing outputs, and run the applicable final ladder. A later executable edit invalidates that candidate. Do not keep the old `ae1d03f` identity as current by excluding a new compiled probe from the gate.

If the finalization genuinely changes only documentation/evidence, the prior implementation-bound ladder can remain prior evidence once unchanged source/build inputs are mechanically established. Label what was inspected versus freshly rerun; do not rerun identical binaries merely to embed another documentation SHA.

Useful entry points (use the configured VS environment, or `pwsh ./verify/verify.ps1 doctor` for discovery):

```powershell
cmake --build --preset debug --target <focused-target>
ctest --test-dir build/debug -R <registered-test-name> --output-on-failure
ctest --preset debug
ctest --preset release-test
cmake --preset analyze
cmake --build --preset analyze
pwsh ./verify/Invoke-CiParity.ps1
pwsh ./dev.ps1 test-e2e
pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=core
pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=all
pwsh ./verify/visual/Invoke-VisualCatalog.ps1 -Gallery
openspec validate performance-closure-and-multisurface-acceleration --strict --no-interactive --json
openspec validate --all --strict --no-interactive --json
```

Derive the focused many-tabs, identity/provenance, governance, status/lifecycle, hygiene, unsigned qualification, and ASan commands from current harness definitions. The authoritative ASan slice is `ffprotocol_fuzz_tests`, `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`, and `ffvolumescanner_terminal_tests`; add relevant focused ASan tests for the changed code. Fix an absent ASan runtime in the subprocess environment rather than treating loader failure as a product defect or skipping the slice.

Record actual registered pass/fail/skip counts and all binary identities; **127/127 is historical unless freshly obtained**. Check practical user-visible paths after retained engine/search changes; do not infer a UI speedup from an engine timer. Keep local parity labelled **`LOCAL-PARITY-NOT-HOSTED-CI`**.

Retain initial failures and follow `AUTONOMOUS.md`'s bounded retry/flaky-test policy. A retry pass alone is not closure. The visual readiness change is a **bounded mitigation**, with historical causality unverified; do not restore the disproven root-cause claim. Do not weaken the existing **30-second** assertion in `TestStopReturnsWithoutWaitingForTheSchedulerInterval`. Stop only campaign-owned/disposable processes, never arbitrary user work to manufacture an idle host.

## 6. Reconcile acceptance and prove the actual final tip

After acceptance and reviews, reconcile the memory evidence, task 8.1 completion note, 8.3 decision, relevant historical/current task annotations, final-report sections A–M, lifecycle, and top WORKSTATE checkpoint. Fix contradictory current completion/provenance statements without mass-rewriting historical records. Mark tasks complete only against evidence and update lifecycle according to its schema; neither a structural OpenSpec pass nor a `completed` lifecycle alone establishes main readiness. Do not automatically archive or start another campaign.

Commit the last executable-impacting change before certification. Commit all remaining tracked documentation, task, evidence, and WORKSTATE edits before the final equivalence check. Stage only owned files; verify scope, line endings, `git diff --check`, and a clean tree.

Run the corrected gate from the certified implementation to the **actual resulting tracked HEAD**; use an absolute repository root and a current compile database:

```powershell
$repo = (Get-Location).Path
$tip = (git rev-parse HEAD).Trim()
pwsh ./verify/qualification/Test-ExecutableTreeEquivalence.ps1 `
    -RepoRoot $repo -ImplementationSha <certified-implementation-sha> -EvidenceSha $tip `
    -CompileCommandsPath build/release-test/compile_commands.json `
    -OutputPath build/finalize-for-main-equivalence.json -Format json
```

Inspect the result's exact identities, derived coverage, digest, verdict, and changed-input lists. A timeout or absent artifact is not a pass. Do not weaken derivation or redefine the source set to make a failure pass. Reuse prior adversarial gate-control evidence only if the gate/assumptions are unchanged; exercise relevant negative/positive controls when changing them.

Any subsequent tracked commit, including a checkbox or WORKSTATE edit, requires a new gate ending at that new tip. Keep the final live SHA/digest/verdict in **generated evidence, the PR, or the session response**, never another tracked annotation requiring its own containing SHA. A gate proves source/build-input equivalence, **not reproducible binaries**. Avoid the certify-then-document loop.

Fetch and reconcile once more, push the **feature branch without force**, verify local HEAD equals its fetched remote, and update the existing PR rather than opening a duplicate. Remain on the unmerged feature branch. Report the actual pushed SHA, certified identity, final gate artifact/digest, and reviewer dispositions.

## 7. Main-readiness gate and external stop conditions

Inspect hosted CI for the final pushed SHA once; allow a bounded wait for a nonterminal run, but do not rerun known-unstartable workflows. Inspect the applicable branch protection/ruleset and required checks/reviews read-only. GitHub `MERGEABLE` means no conflicts, not acceptance. If policy inspection is denied, report that uncertainty rather than assuming there are no required gates.

- Executed repository-caused CI failure: reproduce, fix, recertify the affected/new candidate, and inspect its new tip.
- Zero-step infrastructure failure: retain **`HOSTED_CI=BLOCKED_EXTERNAL`**, job/step evidence and the exact external action needed. Do not infer billing from missing annotations, modify billing/permissions/protection, disable checks, or relabel local parity as hosted success.
- An unavailable required hosted check or required review blocks **actual main readiness**. Local acceptance can be complete while main readiness remains NO. Do not bypass branch policy or claim all issues are resolved because only an external blocker remains.
- Production signing, trust stores, clean-host install, elevated MFT/USN, the raw-volume completion bound, physical display/guest/provider evidence remain outside this ordinary unprivileged finalization. Do not turn them into new local requirements unless an applicable contract actually requires that evidence for this main update. Keep predecessor raw-volume task 5.4 and residual task 2.3 open without their required evidence.

Never read/glob `verify/runs/`, `verify/baselines/`, `verify/.signing/`, or auxiliary `.kilo/**` worktrees. Preserve the three-process security split, closed privileged command surface, parser bounds, signed-peer checks, immutable-generation ownership, degraded operation, and fail-closed production signing configuration. Read the foundation design before touching IPC/security/process boundaries. No privilege provisioning, certificates, Administrator requirement, or signing-pin changes.

Work until all repository-owned acceptance/review issues are closed and the final candidate is certified, or an evidenced external/infrastructure blocker prevents the remaining required step. Do not loop on unchanged failures or silently relax acceptance. Checkpoint the exact frontier and stop honestly when blocked.

## 8. Required terminal handoff

Deliver a concise final report and PR update containing:

1. Task progress and lifecycle; complete memory decomposition/rebuild proof and the measured retain/reject decision.
2. Separate Standards and Spec dispositions, fixing commits, and any remaining actionable findings.
3. Exact commands, actual certification counts, fresh-versus-inspected evidence, retained failure/flake records, and unavailable boundaries.
4. Implementation SHA, embedded binary SHA, evidence/repository tip SHA, fetched main base, pushed feature SHA, final equivalence digest/verdict/artifact, and clean-tree confirmation.
5. PR URL, live hosted run/check facts, applicable required-review/check status, and exactly one next action if blocked.
6. Explicit verdicts: **`LOCAL_ACCEPTANCE=PASS|FAIL|BLOCKED`**, **`HOSTED_CI=PASS|FAIL|PENDING|BLOCKED_EXTERNAL`**, **`MAIN_READY=YES|NO`**, and **`MERGE_ELIGIBLE=YES|NO`**, each with reasons. YES requires all applicable local acceptance and required merge gates satisfied; it never authorizes self-merging.

Success is a clean, remotely pushed, independently reviewed, truthfully certified candidate that can be moved to main under actual policy when separately authorized. An external-blocked handoff is useful progress, but must not be labelled successful main readiness. **Do not perform the main update in this session.**
