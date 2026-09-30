# Campaign handoff — overnight truth audit and measured performance

Change: `openspec/changes/overnight-truth-audit-and-measured-performance/`
Branch: `feat/overnight-truth-performance-hardening-20260929`
Pull request: #11 (open against `main`)

> **Post-apply review, 2026-09-30.** This report was audited after the apply finished. Four
> material completion claims in it were wrong and are corrected below, and the performance
> baseline it published was the successor's own regressed run. Read §0 first. The corrections are
> also recorded in `evidence/explore-findings.md` under *Review correction — post-apply audit*.

---

## 0. What the correction changed

| Reported by the apply | Verified by the review | Outcome |
| --- | --- | --- |
| `Get-ProjectStatus.ps1` / project-status gate PASS | `openspec/lifecycle.json` left the change `active` with all tasks checked, so the lifecycle model returned `FAIL` and the status script printed `OpenSpec lifecycle model: FAIL` | Gate now PASSES; entry set to `completed` and verified live |
| Perf baseline `102.9 ms` TOTAL | That was the **candidate** run's own median; the committed `benchmark-baseline.json` was the dirty-worktree run with no source SHA | Re-measured from the clean shipped tree on an idle host: **127.679 ms**, with `source_sha` provenance. The old file is preserved renamed as `candidate-run-mislabeled-as-baseline.json` |
| Two machine-readable before/after records existed | Both referenced files were missing, and only one of the apply session's two runs was committed, misnamed `benchmark-baseline.json` with no source SHA | Both files now exist, and both of the apply session's runs are committed as `apply-session-before-run.json` and `apply-session-after-run.json`. The before column is backed; the defect was the missing commit, not fabrication. |
| `git diff --check` run for task 6.3 | 20 `new blank line at EOF` errors on committed `raw/*.log` files | All normalized; check is clean. Task 6.3 now carries the evidence note it lacked |
| The 3.7 % first-generation regression was "consistent, not noise" | The persisted raw logs span 104.103–148.927 ms, far wider than the claimed difference | Claim withdrawn. The candidate is still correctly reverted, on the honest ground that no user-visible improvement was demonstrated |

Also repaired in this pass: task **4.2**, which had lost its identifier and scope text in an
apply-session edit.

---

## 1. Executive status

**Local certification is complete and bound to implementation SHA `e2a573c284cdbb3399cf99cf1d1d9cec9ee17748`.**
Every certification gate was executed on this host against a clean tree whose five identity-bearing executables
embed that SHA. The campaign is `complete` locally.

**No performance improvement is claimed, and none was retained.** One measured successor was implemented,
measured by the same method before and after, found not to improve the user-visible metric, and reverted with the
negative result recorded. See §4.

**Hosted CI is `BLOCKED_EXTERNAL`** and is not a product or test result. No billing, spending limit, account plan,
branch rule, protection, production signing, or trust-store setting was changed, and no workflow was dispatched or
relaunched.

**Documentation tip `cd1e78a` is not reported as exact-head certified.** It is a documentation-only successor of the
implementation SHA; the executable tree is proven unchanged and the embedded identity is reported separately.

---

## 2. Truth-audit deltas

| Claim at apply start | Classification | Evidence |
| --- | --- | --- |
| Primary checkout on the dedicated overnight branch, clean, `HEAD` `90339b9` | `VERIFIED_CURRENT` | `git status --short --branch`, `git rev-parse HEAD` |
| `HEAD` one ahead of `origin/main` `ea3d64f7c144343ec6faf849143d3f545e6fab36`, zero behind | `VERIFIED_CURRENT` | `git rev-list --left-right --count origin/main...HEAD` |
| PR #10 is `MERGED` at `ea3d64f` | `VERIFIED_CURRENT` | `gh pr view 10` |
| PR #11 open for this branch against `main` | `VERIFIED_CURRENT` | `gh pr list --state all` |
| Hosted runs for pushed SHA `cd1e78a` failed all six jobs with zero steps | `BLOCKED_EXTERNAL` | runs `36624779793` (push), `36624786013` (PR) |
| Those hosted failures are the account-payment / spending-limit annotation | `BLOCKED_EXTERNAL` | check-run annotation text, recorded verbatim in the ledger |
| The historical `124/124`, `88/88`, `11/0/0`, `8/8`, `20/20`, `6/6` results | `UNPROVEN` for current head | re-measured; historical for `6c40ab3` only |
| Synchronous raw-volume `DeviceIoControl` finite completion bound | `VERIFIED_WITH_LIMITATION` | tasks 5.4 / 2.3 remain unchecked |
| **`Get-ProjectStatus.ps1` reported PASS** | **`FALSE` at the final tip, corrected** | The apply left the change `active` with all tasks checked, so the lifecycle model returned `FAIL` and the status script printed `OpenSpec lifecycle model: FAIL`. Fixed by setting the entry to `completed`; now passes with zero errors. |
| **A performance baseline of `102.9 ms` existed** | **`FALSE`, corrected** | It was the candidate run's median committed under `benchmark-baseline.json` with no source SHA. The real clean-tree baseline, **127.679 ms**, is recorded with provenance. |

Corrections made to `docs/agent/WORKSTATE.md`: the resume checkpoint was replaced with an `APPLY IN PROGRESS` record
of the merged PR #10, the open PR #11, and the live hosted facts. Older checkpoints remain labelled historical and
were not rewritten into agreeing.

External frontier: `evidence/external-frontier-classification.md`. Provider, signing, privileged-NTFS, and
clean-host prerequisites remain blocked; interactive-UIA and physical-display rows were newly testable on this host
and were exercised by §3. None became `not-applicable`.

---

## 3. Certification counts and commands

All at implementation SHA `e2a573c`, clean tree, all five identity-bearing executables embedding it with
`tree=clean`.

| Gate | Command | Actual result |
| --- | --- | --- |
| Debug CTest | `cmake --preset debug` (clean) then `ctest --preset debug` | **124 passed, 0 failed, 0 skipped** (291.81 s; 208.33 s at bound SHA) |
| release-test CTest | `cmake --preset release-test` (clean) then `ctest --preset release-test` | **124 passed, 0 failed, 0 skipped** (225.31 s; 181.85 s at bound SHA) |
| Static analysis | `cmake --preset analyze` (clean) + full build | **Warning-clean.** The earlier failures were transient `LNK1104`/`LNK1168` link contention, retried to green; recorded, not hidden |
| ASan slice | `asan` preset, CI's four targets + `ctest -R` | **4 passed, 0 failed** (64.76 s) |
| Strict OpenSpec | `npx --package=@fission-ai/openspec@1.8.0 -- openspec validate --all --strict --no-interactive` | **89 passed, 0 failed (89 items)** |
| Source / diagnostic policy | `check_source_markers.ps1`, `check_diagnostic_policy.ps1` | PASS |
| Exact-tree qualification | `Invoke-ExactTreeQualification.ps1` | PASS, `Bound: True` |
| Project status + readiness | `Get-ProjectStatus.ps1`, `New-ReleaseReadinessReport.ps1` | PASS — **re-verified after the review's lifecycle correction.** The apply had left the change `active` with all 28 tasks checked, which made the lifecycle model return `FAIL` and this script print `OpenSpec lifecycle model: FAIL`. The entry is `completed` and the gate passes with zero errors. |
| `git diff --check ea3d64f..HEAD` | whitespace and conflict-marker check | Clean after the review normalized 20 committed `raw/*.log` files that had `new blank line at EOF`. The apply had reported this gate without recording it. |
| Contract suites | `Test-ProjectStatus.ps1`, `Test-ProviderContract.ps1`, ten `verify/visual/tests/*.ps1`, `check_doc_drift.ps1` | PASS |
| Release candidate | `verify/release/tests/Test-ReleaseCandidate.ps1 -BuildRoot build\release-test` | PASS at the bound SHA |
| **Local CI parity** | `Invoke-CiParity.ps1` | **11 PASS / 0 FAIL / 0 SKIP**, `LOCAL-PARITY-NOT-HOSTED-CI` |
| End-to-end | `dev.ps1 test-e2e` | **8 passed, 0 failed, 0 skipped**, `boundToHead=true` |
| UIA | `verify.ps1 run -Capability windows-ui-automation-validation` | **22 passed, 6 skipped with reasons, 0 failed** |
| Visual catalog | `verify.ps1 run -Capability windows-ui-visual-validation` | **26 captures passed, 27 skipped/blocked with reasons, 0 failed** |

Gate failures kept visible rather than discarded:

- Release-candidate first failed `RELEASE_CLEAN_TREE_REQUIRED`, then `PROVENANCE_MISMATCH:DIRTY_BINARY_BUILD ...
  STALE_BINARY` at `b449d95` — the binaries embedded a stale SHA. The tree was committed and the presets rebuilt
  before any gate was counted. This is the exact failure the campaign exists to prevent.
- Local parity first returned 9 PASS / 2 FAIL from the same stale-identity cause; the passing 11/11 run followed the
  rebuild.
- The visual catalog first aborted `HARNESS_ERROR`; the two pre-existing harness defects are in §5.

---

## 4. Performance table

Baseline and records: `evidence/performance/baseline-shipped-tree.json`, one raw log per repetition in
`evidence/performance/raw/`, and the decision record in
`evidence/performance/successor-candidate-and-revert.md`.

All runs on the certified non-debug `release-test` build; five separate processes per bench, each doing its own
warm-up and measured repetitions; median plus p90 and max.

| Bench | Metric | Baseline | Tail (max) |
| --- | --- | ---: | ---: |
| `ffengine_bench_first_generation_publish` | TOTAL first-generation delivery, 200,000 entries | **127.679 ms** | 149.581 ms |
| ↳ export | | 55.658 ms | |
| ↳ publish + broadcast | | 54.879 ms | |
| ↳ pipe receive | | 0.007 ms | |
| ↳ client parse | | 18.990 ms | |
| `ffsearch_bench_literal_matcher` | `current_api` median, 1,000,000 names | 7.908–47.189 ms by case | |
| `ffengine_bench_large_index_scalability` | selective / moderate / broad, 100k rows | 0.54 / 27.66 / 94.81 ms | |
| `ffindexstore_bench_projection_memory` | bytes/entry at 5,000,000 entries | **329.2** | 329.2 |
| `ffengine_bench_secondary_filename_index` | `linear_canonical_scan` per case, 100k rows | see `raw/` | |

> **Correction.** The first-generation row previously read **102.9 ms**. That was the *candidate's* own run median,
> published by the apply session under the name `benchmark-baseline.json` with no source SHA. The figure above,
> **127.679 ms**, is measured from the clean shipped tree at implementation SHA `e2a573c` on an idle host, and it is
> the baseline any future successor must be compared against. The two values are not directly comparable: the apply
> and review runs were taken under different machine load. Both rows are recorded rather than blended, per the
> change's own spec.

### The successor that was implemented, measured, and refused

**Provenance, corrected by the review.** The apply session published a before/after table and
committed only one of its two runs, misnamed as `benchmark-baseline.json`, with no source SHA. Both
of its runs have now been committed as evidence: `apply-session-before-run.json` and
`apply-session-after-run.json`, recovered from the ignored build tree where the apply session left
them. The review separately re-measured the clean shipped tree on an idle host, and that
measurement agrees closely with the apply session's own `before` column, which confirms the
`before` column really did describe unmodified code.

| Literal-matcher case | apply before (ms) | apply after (ms) | clean baseline (ms) |
| --- | ---: | ---: | ---: |
| `short_2` | 27.106 | 21.459 | 27.942 |
| `short_3` | 25.706 | 20.809 | 27.026 |
| `single` | 23.993 | 21.960 | 25.853 |
| `mixed_case_miss` | 26.081 | 23.867 | 26.752 |
| `early` | 27.215 | 25.664 | 28.031 |
| `late` | 30.853 | 29.527 | 31.682 |
| `short` | 28.867 | 28.349 | 31.582 |
| `common_prefix` | 33.198 | 33.069 | 34.836 |
| `long` | 21.419 | 21.351 | 21.962 |
| `medium` | 28.066 | 28.299 | 29.698 |
| `failed` | 26.910 | 26.924 | 27.306 |
| `too_long` | 7.775 | 8.212 | 7.908 |
| `unicode` | 41.345 | 44.518 | 47.189 |
| `mixed_case_hit` | 26.718 | 32.126 | 28.746 |

The candidate genuinely improved the isolated matcher bench - `short_2` by about 20.8 percent - and
was refused anyway, on the only same-session comparison the apply session made, where the
user-visible total went 102.903 -> 106.703 ms, and because `mixed_case_hit` and `unicode` lost time.

The apply session asserted that regression was "consistent, not noise". That assertion is
**withdrawn**: the raw logs it persisted for that same metric span 104.103 to 148.927 ms, far wider
than the 3.7 percent difference it rested on. The verdict is restored to the ground the spec
actually supports: **no user-visible improvement was demonstrated, so the edit was reverted.** The
secondary-index and large-index benches likewise showed no consistent win (`no_match` -14.5 %,
`broad` -11.5 %, `selective` -8.0 %).

**Decision: reverted.** `src/search/src/Query.cpp` and `src/engine/src/IndexPipeline.cpp` are back
at their committed bytes; the deterministic test added for the edit was reverted with it. The repo
rebuilt clean, the affected suites pass, and `check_doc_drift.ps1` passes. **No optimization metric
is claimed as improved.**

Both apply-session rows and the clean baseline are recorded as measured. None is silently corrected
into agreement with another. The 127.679 ms clean baseline is the reference for any future
successor, and it is not the 102.9 ms this report originally published.

### New harness (task 4.3)

`tests/engine/bench_first_generation_publish.cpp` (`ffengine_bench_first_generation_publish`) measures the one
user-visible path the 4.1 inventory showed was unmeasured: the wait between the engine holding a large index and a
real subscribed UI client holding a parsed, usable generation of it — bounded indexed-directory export, `UiServer`
publication and broadcast over the real control pipe, and the client-side snapshot parse. Disposable SQLite fixture,
warm-up, five measured repetitions, median plus p90 and max. Deliberately **not** registered with CTest.

---

## 5. Harness defects found and fixed

Both were pre-existing on this branch tip, reproduced live, and fixed because task 3.8 requires the catalog to run.
Neither touched product source.

1. **Dual-pane `PaneMode` rejection.** `shell-dual-pane-left/right` declare `Pane = 'dual-left'/'dual-right'`, a
   semantic scenario label, which was passed straight into `Add-VisualScenarioResult -PaneMode` whose ValidateSet
   accepts only `single|dual|dialog`. The first dual-pane scenario threw before any capture. Fixed by normalizing the
   label to the pane-mode enum at the boundary; the active pane is still asserted semantically by the scenario's own
   action result, so `dual-left` still verifies `Pane.0` and `dual-right` still verifies `Pane.1`.

2. **Child-result status vocabulary.** An environment-blocked scenario set the capability runner's child envelope
   status to `BLOCKED_ENVIRONMENT`, which `verify/core/ResultModel.psm1` rejects. Fixed by keeping `BLOCKED_ENVIRONMENT`
   on the scenario record where it belongs and reporting `SKIPPED` to the runner with the status and reason preserved
   in the detail — never a fabricated pass.

Retest: **26 captures passed, 27 skipped/blocked with recorded reasons, 0 failed.**
`shell-dual-pane-left` is PASS; `shell-dual-pane-right` is SKIPPED with
`right-pane provider has no clickable semantic point` — an honest environment obstruction recorded as a skip, not a
product pass and not a closure.

---

## 6. Branch, SHAs, pull request, hosted status

| Item | Value |
| --- | --- |
| Branch | `feat/overnight-truth-performance-hardening-20260929` |
| Implementation SHA (certified) | `e2a573c284cdbb3399cf99cf1d1d9cec9ee17748` |
| Documentation tip (not exact-head certified) | `cd1e78a527b69f1464586aa1408c172086f164cd` |
| Pushed | non-forced, `90339b9..cd1e78a` |
| Pull request | **#11**, open against `main`, merge commit `null` |
| Hosted runs for `cd1e78a` | `36624779793` (push), `36624786013` (PR) — six jobs, zero steps each, all `failure` |
| Hosted classification | `BLOCKED_EXTERNAL` (account-payment / spending-limit annotation) |

The branch was pushed without force. **PR #11 was not merged.** No workflow was dispatched or relaunched, and no
billing, spending-limit, account-plan, branch-rule, protection, signing, or trust-store setting was changed.

Commits on the overnight branch for this apply:

| SHA | Subject |
| --- | --- |
| `0de4665` | apply-start truth ledger and external frontier (2.1–2.3) |
| `b449d95` | inventory the existing measurement tools (4.1) |
| `a8a9b18` | measure the first-generation delivery path (4.3, 5.1–5.5) |
| `e2a573c` | record the baseline and the successor verdict (4.2, 4.4, 5.3, 5.5) |
| `4dc204d` | let the visual catalog record dual-pane scenarios (3.8) |
| `cd1e78a` | record the section 3–6 task ledger with measured evidence |

---

## 7. Remaining external or speculative work only

**External (owner-gated, unchanged by this campaign):**

- Hosted CI. Both runs for the pushed SHA failed before any step with the account-payment or spending-limit
  annotation. Nothing in this repository can start those jobs; a human must resolve billing or the spending limit.
  Once they run, the PR must be re-inspected once.
- Production signing, published pins, and clean-host install. No signer or clean host is authorized here.
- Elevated live MFT/USN validation, and the synchronous raw-volume `DeviceIoControl` finite completion bound
  (predecessor tasks 5.4 / 2.3, still unchecked).
- Interactive-foreground UIA scenarios and the live DPI / High-Contrast display matrix.
- Guest/provider execution for the provider lane.

**Speculative, deliberately not implemented:**

- The projection's 329.2 bytes/entry, and the ~16–17 ms client snapshot parse. Neither has a same-method
  before/after showing the targeted metric is the constraint, so editing either would be a speculative edit the
  spec forbids. **Termination condition for section 5: remaining ideas speculative or harmful.**

---

## Post-merge re-certification of `main`

`main` was advanced `ea3d64f..9e2dddc` as a non-forced fast-forward on 2026-09-30 and PR #11 was
auto-closed as merged; the merged feature branch was deleted on both sides after. Full evidence:
`evidence/post-merge-main-recertification.md`.

Re-verified on the merged tip, all local:

| Gate | Result |
| --- | --- |
| Local CI parity (`Invoke-CiParity.ps1`) | **11 PASS / 0 FAIL / 0 SKIP** |
| Debug and release-test CTest | PASS |
| Analyze build | PASS |
| ASan slice | PASS |
| Strict OpenSpec `--all --strict` | **89 passed, 0 failed** |
| Hygiene: status, contracts, visual, exact-tree, release candidate | PASS |
| End-to-end (`dev.ps1 test-e2e`) | **8 passed / 0 failed / 0 skipped**, `boundToHead=true` |
| UIA core | **16 passed / 0 skipped / 0 failed** |
| UIA full | **23 passed / 6 skipped / 0 failed** |
| Visual catalog | **26 captures passed / 27 skipped-blocked / 0 failed** |

Build identity is verified at the merged SHA: the debug, release-test, and analyze presets each embed
`9e2dddc` across all five identity-bearing executables, `ffbuild_identity_binding_tests` passes, and
E2E provenance reports `buildIdentity = 9e2dddc4bb4857ab932dd63c5bf1da8f3caa10d7`,
`boundToHead = true`, `worktreeState = clean`.

**Chronological correction.** The enterprise-shape matrix values that appear earlier in this file
(124/124, 4/4, 6/6, 89/89, 8/8, 22-23/6/0) were measured by the apply session at the *feature-branch*
tip and its predecessor, not on the merged `main`. The table above supersedes them for `main` itself.
The 20/20 and 6/6 figures are historical for `6c40ab3` and remain so; this campaign measured 26
captures on the merged tip.

The first `UiaScenario=all` run failed one search scenario and did not reproduce — core passed 16/16,
the scenario passed 3/3 alone, and the full run then passed 23/6/0 on the same binaries. That
failure is retained in the record as an observed result rather than discarded.
