# Final report — performance-closure-and-multisurface-acceleration

Sections A–M per the campaign brief. The live post-push tip, equivalence
verdict, and PR URL are recorded in the PR body and the session response
outside this checked commit, per the reordered 11.6 rule.

## A. Repository truth

- Branch: `feat/performance-closure-and-multisurface-acceleration-20261001`.
- Start: `origin/main` `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3` (unchanged
  through the campaign; the branch was created from it).
- `IMPLEMENTATION_SHA`: the finalization's last executable-impacting commit
  `fc8d873b25417ad5986d9853c3a4ac2298a1350b` (independent-review fixes on top
  of the decomposition seam, the bulk-rebuild slack release, the dense LRU
  representation, and the benchmark provenance work; the last executable
  change). Later tracked commits in this finalization are documentation,
  evidence, and the tracked reproduction runner only. History: `ae1d03f0` was
  the pre-finalization certified implementation, `43c044f` the decomposition
  seam + slack release, `53c1a8d` the dense LRU, `3651dfb` the probe SAL fix.
- `certified_binary_sha` (embedded identity of the rebuilt outputs): the
  executable tree of `fc8d873` (equivalence-gated to the final tracked tip);
  the identity-bearing binaries are rebuilt at the certification tip and the
  live tip/verdict are recorded in the generated equivalence artifact and the
  PR body, not in this tracked file.
- Certified binary identity: every preset was rebuilt at the final tip
  before the ladder; identity binding/freshness tests pass inside the
  parity CTest legs.
- OpenSpec lifecycle: this change is **`completed` with 51/51 tasks checked**
  after the finalization closed task 8.1 (the allocation-instrumented
  projection-memory decomposition) and re-decided task 8.3 on that evidence
  (section H). No internal acceptance work is open. The change is **not
  archived and not merged**: PR #13 remains open for human review by design,
  with `MERGE_ELIGIBLE` stated in the PR body and the session response.
  Predecessor
  `measured-performance-acceleration-and-exact-certification` `completed`
  with its 6.3/6.4 closed by current evidence (task 2.7).
- Worktree: clean at every certification run; the two owned temporary
  control worktrees/branches are removed before the push.

## B. Certification debt (current counts, this candidate)

Fresh full ladder at `ae1d03f` (all counts are the actual registered totals;
the predecessor-era 124/124 is historical evidence for `8c8aa0b`, not the
current suite):

| gate | result |
|---|---|
| Local CI parity (`Invoke-CiParity.ps1`) | **11 PASS / 0 FAIL / 0 SKIP** (preflight, policy, strict OpenSpec, build debug/release-test/analyze, ctest debug, ctest release-test, ASan slice, hygiene suite, release-candidate) |
| Debug CTest | **127/127** passed, 0 failed, 0 skipped |
| release-test CTest | **127/127** passed, 0 failed, 0 skipped (the 30-second volume-session assertion unchanged and passing) |
| Analyze | warning-clean (0 warnings) |
| Authoritative ASan slice | 4/4 passed |
| strict OpenSpec | 91/91 |
| hygiene suite (status/contracts/visual/exact-tree) | PASS |
| unsigned release-candidate qualification | PASS |
| E2E (`dev.ps1 test-e2e`) | 8 passed / 0 failed / 0 skipped, `boundToHead=true` |
| UIA core | 16 passed / 0 skipped / 0 failed |
| UIA full | 23 passed / 6 skipped / 0 failed (one prior attempt died with the environmental `capability-worker-exited-without-result`; the retry passed) |
| visual catalog (`-Gallery`) | 20 recorded / 20 passed / 0 failed (one prior attempt aborted on "settings dialog did not appear". The harness change is a **bounded mitigation**, not a proven root-cause fix: `Set-CatalogTheme` used a single 2.5 s dialog probe and now uses a bounded 10 s readiness wait in 1 s polls, so a delayed dialog is tolerated up to the documented bound; isolated controls verify delayed-dialog recovery and bounded failure, but the historical abort's precise cause was not captured) |
| focused many-tabs | 1/1 passed (one prior attempt hit the same environmental worker death; the retry passed) |

Predecessor closure evidence (task 2.7): release-test 124/124 with
`TestStopReturnsWithoutWaitingForTheSchedulerInterval` at 13.99 s, Debug
124/124, Analyze 0 warnings, ASan 4/4, E2E 8/0/0, UIA 23/6/0, visual 20/20
on the pre-optimization candidate `8c8aa0b`.

## C. UIA flake

`search-deep-result-navigation` reproduced once in the first aggregate run
(`alpha` not accepted, observed empty on AutomationId 7101), then passed in
5 focused trials, 7 group aggregates, the final aggregate, and both final
certification UIA runs. Classification **`UNREPRODUCED / OBSERVED FLAKE`**
with both failures retained. Bounded failure diagnostics were added to
`Set-UiaSearchQuery` (edit-child discovery, combo hwnd start/end, native vs
UIA read-back split, retry state) so a future failure self-identifies.
Two aggregate `HARNESS_ERROR` worker exits were environmental (0.5 GB free
RAM) and are recorded, not hidden.

## D. Equivalence gate

`verify/qualification/Test-ExecutableTreeEquivalence.ps1`, current
validation `evidence/equivalence-gate-validation-v3.json` (8 controls):

- docs-only PASS (389 paths, 309 TUs, 33 build inputs);
- compiled `.cpp`, header-only, nested-header, CMake-manifest, and
  **template-only** (`cmake/FastFilesVersion.rc.in`) changes FAIL naming the
  exact input;
- unresolved quoted include FAILs closed naming the include and TU;
- deleted derived input FAILs with `derived-path-missing-in-a-commit`.

The validation exercise found and fixed four earlier gate defects
(line-array coercion truncating the include closure, `GENERATED` escape
bypass, un-normalized `..` roots, missing includer-directory quoted
resolution) and, under review, the template false PASS plus robustness
defects. Claim boundary (design D4): git-controlled executable-impacting
source/build-input equivalence, **not** reproducible binaries; compiler,
SDK, environment definitions, unmodeled flags, and toolchain binaries are
outside. The historical 228-path record and the v2 record are preserved as
historical (v2's C1 SHA corrected to the real commit).

## E. Allocation isolation

D5 matrix (30 idle-gated, core-pinned, high-priority interleaved processes,
200k entries, payload identity mechanically enforced by memcmp self-validation):
control total 62.29 ms (n=12) vs export-eliminated 35.12 ms (n=6), **-43.6%
with non-overlapping distributions**; build -60%, serialize -56% at
byte-identical payload (23,163,092 bytes). Parse-side elimination (-13.5%)
overlapped the control distribution and stayed inside the same-binary
spreads → **outcome C for the parse side**; outcome A for the export side.
The historical short-name probe is labelled **CONFLATED ALLOCATION +
PAYLOAD-SIZE EXPERIMENT** with all original values preserved.

## F. Name-path optimization — RETAINED

Pooled name representation through export → merge → publish (no per-entry
owning `std::wstring`; wire format version 3 unchanged, byte-identity
proven). Retention evidence:

- in-session same-binary floors: 0.37% (pre) / 0.92% (post) between-arm gaps;
- counterbalanced A/B: **-12.09%** (AB) and **-13.37%** (BA, arms swapped),
  combined **-12.73%**, complete distribution separation in both orderings;
- stage split export 40.03→34.41, publication 25.43→18.13 ms;
- lifetime proofs (40x generation replacement with a stalled reader, bounded
  teardown, independent client mapping after Stop, concurrent readers,
  copy/move byte identity) pass in debug and ASan;
- an A/B iteration caught and fixed a real O(n²) builder defect before the
  retained result (recorded in `evidence/retention-ab-pooled-name-path.md`).

## G. Search — RETAINED

Bounded top-N admission plus the capped-truncation correctness fix
(`ExecuteSearchCore`/`ExecuteSearchViews` had retained the worst N).
Measured (3 interleaved rounds per arm, same-binary floor measured 5-9% for
scan-floor classes): **broad -50.9%, extension -63.5%, short-prefix -51.6%,
high-limit -51.9%, worst-substring -45.9%, mixed-case -26.8%, prefix
-17.5%, moderate -16.2%, Unicode -10.9%**; selective/exact/no-match inside
the measured floor. The filename index is on none of the measured paths.
The admission is proven result-preserving by `ffsearch_admission_tests`
(every sort field/direction; a classifier property test over empty/mixed-
case/non-ASCII/dotted corpora; unbounded never admits) — that test exposed
both the empty-primaryTerm classifier divergence and the incomplete
comparator fix.

## H. Projection memory — DECOMPOSED AND PARTIALLY RETAINED

The finalization completed task 8.1 with allocation-instrumented,
capacity-based decomposition at 200k and 1M rows
(`evidence/projection-memory-decomposition-final.md` +
`evidence/projection-memory/projection-memory-{200k,1m}.json`, produced by
the tracked `run-decomposition.ps1` runner on a clean `release-test` tree).
The reports separate process private, working set, shareable pages, live
requested/caller-usable bytes, allocation counts, and the estimator sum; every
required contributor is a live capacity measurement; normalized-name and
stored-path storage are explicit structural zeros; the residual names its
boundary (probe header, stdlib node links, heap block headers/retention,
SQLite's C plane, page tables) instead of attributing it to fragmentation.

Task 8.3 retained two measured corrections found by that decomposition: the
bulk-rebuild capacity-slack release (name arena/table, flat child array,
trigram postings) and dense per-slot LRU timestamps replacing a per-entry
`unordered_map` node. At 1,000,000 rows, together they remove 94,578,706 bytes
(-90.20 MiB, -20.5%) of still-owned live bytes and 143,572,992 bytes
(-136.92 MiB, -22.7%) of process private bytes, with repeated-rebuild time
unchanged (both comparison arms archived) and content preserved; the spec's 10%
strong target is cleared. The
identifier-map capacity (forced by the <=75% occupancy growth bound), the
filename accelerator (spec-protected, proven used), and the remaining stdlib
node links are rejected/deferred with those measured reasons. History: the
earlier 520.5 B/row / 404.4 B/row / +116.1 MB record in
`evidence/projection-memory-decomposition.md` is preserved and is superseded
only as the pre-instrumentation measurement.

## I. User-visible effect

E2E on the retained candidate: navigation/Home/terminal-state/back-forward/
stale-column checks and exact/partial/no-result/superseded-query/selection/
result-open search checks all pass (8/0/0, `boundToHead`). The engine
measurements are separated from UI claims; no new wall-clock gate was
introduced. The retained search fix also corrects what capped results
contain (top-N instead of worst-N), which is a user-visible correctness
improvement.

## J. Reverted / rejected ideas

- Packed snapshot serialization and in-place row construction: not repeated
  (recorded noise-floor rejections; no new profile showed a different
  dominant cost).
- Parse-side zero-copy representation: inside noise → outcome C.
- Projection-memory changes: rejected with the measured tradeoff.
- The first pooled A/B iteration (owning-wrapper bench) was invalidated as a
  measurement defect and re-run production-faithfully.
- The quadratic builder form was caught by measurement, fixed, and re-measured.

## K. Certification relationship

- `implementation_sha`: the finalization's last executable-impacting change
  (the independent-review fixes over the decomposition seam, the bulk-rebuild
  slack release, the dense per-slot LRU representation, and the benchmark
  provenance work; the exact SHA is the finalization commit named in the PR
  body and the session response). The gate relationship from it to the final
  tracked tip is recorded in the generated artifact and the PR body, not in
  this tracked file, per the reordered rule. Historical: `ae1d03f0` was the
  pre-finalization certified implementation; `f0eb28a` -> `623490f` was PASS
  with digest  `f3668f51f36a9245725cce5aaebbeed6e46738d9e458abceeb4be54002ec49ca`
  (`build/_apply26/gates3/r1fix_self_application.json`); `63ddc21` was the
  pre-repair implementation SHA with digest `244d9c4d430059db...`.
- `certified_binary_sha`: the executable tree of the finalization
  `implementation_sha`; the identity-bearing binaries are rebuilt at the
  certification tip and identity binding/freshness pass inside the parity
  CTest legs (the reports in `evidence/projection-memory/` record the
  benchmark's own embedded identity).
- `evidence_sha` / `repository_tip_sha`: the final tracked tip after the
  report commit.
- `executable_impacting_digest` / verdict: the current run is from
  `ae1d03f0bb90b109cd781d07223fd69ec0f8ec7a` to the tracked tip
  `0d247ed51d343db6faf0114a997b239b759fcb9b` — **PASS**, digest
  `c9c660e85d7c744b90146c53424ee22c9876e5cdfc96a71cddf3e571871df021`,
  389 paths (309 TUs, 33 build inputs). Any later tracked commit requires
  another run; the live final tip/verdict stays in the PR body and the
  generated artifact outside the checked commit. Historical: the
  `63ddc21`-based run used digest `244d9c4d430059db...`.

## L. Hosted CI

`36999522143` (feature-branch push at `6bbd92d`) inspected once: six jobs,
zero executed steps → `HOSTED_CI = BLOCKED_EXTERNAL`. The final tip is
inspected once after the push and reported live; local results remain
labelled `LOCAL-PARITY-NOT-HOSTED-CI`.

## Review repair (completion review at `20abb4c`, BLOCK)

The first completion review's four findings were addressed in `f0eb28a` and its
successors (`ae1d03f` carries the final executable state); the second repair
review's findings are addressed in `ae1d03f` and this report:

- **R1 (P1)**: the top-N admission judged a volume-root row by its empty
  pre-materialization view name while the owning candidate uses the
  `prefix + "\\"` display name, so a capped descending filename search
  discarded the winning root (hot `B` vs durable `Z:\`). Root rows are now
  gated out of both the admission and the `AddViewMatched` view-skip.
  `TestCappedRootSearchMatchesColdAndRetainsTopN` reproduces the reviewer's
  exact case red-first (fails with the gate removed) and passes with it,
  across filename/relevance x both directions x caps 1 and 2.
- **R2 (P2)**: task 8.1 was reopened honestly -- the per-category/
  allocation-instrumented and repeat-rebuild pass was not produced, and the
  8.3 rejection is rescoped to the measured evidence. History: that state was
  closed by the finalization, which produced the instrumented decomposition,
  retained the two measured corrections, and set task 8.1 and 8.3 with
  current evidence.
- **R3 (P2)**: this report now carries the fresh actual totals
  (127/127 both presets at the fixed tip; the predecessor-era 124 is
  historical) and the explicit embedded `certified_binary_sha`.
- **S1 (P2)**: the WORKSTATE current checkpoint was rewritten to one
  coherent frontier and one next action with correct SHAs.

**MERGE_ELIGIBLE** is stated with its exact reasons in the PR body and the
session response. History: at this point in the campaign it was NO pending the
reopened memory acceptance work; that work is now complete (section H), and
the remaining gates are the final-tip certification ladder, the equivalence
gate, and any required hosted check or review.

## M. Remaining work

- **Internal acceptance**: none open, and the finalization gates are complete.
  Task 8.1 is complete and task 8.3 is decided with measured retain/reject
  evidence (section H); the independent reviews of the finalization delta are
  complete with their findings repaired; the full local certification ladder
  ran at the final tip; the executable-tree equivalence gate passed from the
  implementation SHA to the pushed tip; and the single hosted inspection found
  a zero-step, externally blocked result. The live tip, gate digest/verdict,
  ladder counts, and verdicts are recorded in the PR body and the session
  response rather than in this tracked file, so no further certify-and-document
  cycle is opened.
- External only: hosted CI execution (billing/infrastructure), production
  signing/pins, clean-host install, elevated MFT/USN, the raw-volume
  completion bound (predecessor 5.4 / residual 2.3 stay unchecked), the
  physical display matrix, and authorized guest/provider work.
- The temporary equivalence-control worktrees/branches are removed before
  the push; no synthetic commits exist on the feature branch.
