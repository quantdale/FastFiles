# Campaign handoff — overnight truth audit and measured performance

Change: `openspec/changes/overnight-truth-audit-and-measured-performance/`
Branch: `feat/overnight-truth-performance-hardening-20260929`
Pull request: #11 (open against `main`)

---

## 1. Executive status

**Local certification is complete and bound to implementation SHA `e2a573c284cdbb3399cf99cf1d1d9cec9ee17748`.**
Every certification gate was executed on this host against a clean tree whose five identity-bearing executables
embed that SHA. The campaign is `complete` locally.

**No performance improvement is claimed, and none was retained.** One measured successor was implemented,
measured by the same method before and after, found to regress the user-visible metric, and reverted with the
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
| Project status + readiness | `Get-ProjectStatus.ps1`, `New-ReleaseReadinessReport.ps1` | PASS |
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

Baseline and records: `evidence/performance/benchmark-baseline.json`, one raw log per repetition in
`evidence/performance/raw/`, and the decision record in
`evidence/performance/successor-candidate-and-revert.md`.

All runs on the certified non-debug `release-test` build; five separate processes per bench, each doing its own
warm-up and measured repetitions; median plus p90 and max.

| Bench | Metric | Baseline | Tail (max) |
| --- | --- | --- | --- |
| `ffengine_bench_first_generation_publish` | TOTAL first-generation delivery, 200,000 entries | **102.9 ms** | 122.4 ms |
| ↳ export | | 42.1 ms | |
| ↳ publish + broadcast | | 43.2 ms | |
| ↳ pipe receive | | 0.006 ms | |
| ↳ client parse | | 16.6 ms | |
| `ffsearch_bench_literal_matcher` | `current_api` median, 1,000,000 names | 7.5–44.5 ms by case | |
| `ffengine_bench_large_index_scalability` | selective / moderate / broad, 100k rows | 0.50 / 26.9 / 94.1 ms | |
| `ffindexstore_bench_projection_memory` | bytes/entry at 5,000,000 entries | **329.2** | 329.2 |
| `ffengine_bench_secondary_filename_index` | `linear_canonical_scan` per case, 100k rows | 2.5–9.6 ms | |

### The successor that was implemented, measured, and reverted

Candidate: consume the already-present `CandidateView::nameAsciiLower` (the projection computed and stored the
matching per-entry value, but `IndexedSearch`'s zero-copy capture discarded it) so the matcher could skip
per-character ASCII folding on the dominant all-lowercase name class.

Same method, same machine, same build, five measured processes:

| Literal-matcher case | before (ms) | after (ms) | delta |
| --- | --- | --- | --- |
| `short_2` | 27.106 | 21.459 | **+20.8 %** |
| `short_3` | 25.706 | 20.809 | **+19.1 %** |
| `single` | 23.993 | 21.960 | +8.5 % |
| `mixed_case_miss` | 26.081 | 23.867 | +8.5 % |
| `early` | 27.215 | 25.664 | +5.7 % |
| `late` | 30.853 | 29.527 | +4.3 % |
| `short` | 28.867 | 28.349 | +1.8 % |
| `long` | 21.419 | 21.351 | +0.3 % |
| `common_prefix` | 33.198 | 33.069 | +0.4 % |
| `failed` | 26.910 | 26.924 | −0.1 % |
| `medium` | 28.066 | 28.299 | −0.8 % |
| `too_long` | 7.775 | 8.212 | −5.6 % |
| `unicode` | 41.345 | 44.518 | −7.7 % |
| `mixed_case_hit` | 26.718 | 32.126 | **−20.2 %** |

User-visible metric moved the wrong way:

| First-generation metric | before (ms) | after (ms) | delta |
| --- | --- | --- | --- |
| export | 42.120 | 44.636 | −6.0 % |
| publish + broadcast | 43.183 | 45.126 | −4.5 % |
| client parse | ~16.6 | ~17.7 | ~−6 % |
| **TOTAL** | **102.903** | **106.703** | **−3.7 %** |

Two further processes on the candidate gave 120.7 ms and 117.9 ms, so the regression is consistent rather than noise.
The secondary-index and large-index benches showed no consistent win (`no_match` −14.5 %, `broad` −11.5 %,
`selective` −8.0 %).

**Decision: reverted.** `src/search/src/Query.cpp` and `src/engine/src/IndexPipeline.cpp` are back at their committed
bytes; the deterministic test added for the edit was reverted with it. The repo rebuilt clean, the affected suites
pass, and `check_doc_drift.ps1` passes. **No optimization metric is claimed as improved.**

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
