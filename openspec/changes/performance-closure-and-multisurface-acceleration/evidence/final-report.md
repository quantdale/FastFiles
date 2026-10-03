# Final report — performance-closure-and-multisurface-acceleration

Sections A–M per the campaign brief. The live post-push tip, equivalence
verdict, and PR URL are recorded in the PR body and the session response
outside this checked commit, per the reordered 11.6 rule.

## A. Repository truth

- Branch: `feat/performance-closure-and-multisurface-acceleration-20261001`.
- Start: `origin/main` `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3` (unchanged
  through the campaign; the branch was created from it).
- `IMPLEMENTATION_SHA`: `63ddc21acf089ad92ddb5783882a74a595a2513c` (bench
  allocator SAL annotations + configure_file prefix stripping -- the last
  executable-impacting commit). `d50c474` ("fix the material review
  findings") is the review-fix commit before it. The 11.6 gate initially ran
  from `d50c474` and correctly FAILED, naming the bench translation unit; it
  passes from `63ddc21`.
- Certified binary identity: every preset was rebuilt at the final tip
  before the ladder; identity binding/freshness tests pass inside the
  parity CTest legs.
- OpenSpec lifecycle: this change `completed` (all 51 tasks checked; the
  lifecycle model requires an active change to have open tasks, and this
  campaign's work is done) — **not archived and not merged**: PR #13 remains
  open for human review by design. Predecessor
  `measured-performance-acceleration-and-exact-certification` `completed`
  with its 6.3/6.4 closed by current evidence (task 2.7).
- Worktree: clean at every certification run; the two owned temporary
  control worktrees/branches are removed before the push.

## B. Certification debt (current counts, this candidate)

| gate | result |
|---|---|
| Local CI parity (`Invoke-CiParity.ps1`) | **11 PASS / 0 FAIL / 0 SKIP** (preflight, policy, strict OpenSpec, build debug/release-test/analyze, ctest debug, ctest release-test, ASan slice, hygiene suite, release-candidate) |
| Debug CTest | 127/127 passed, 0 failed, 0 skipped |
| release-test CTest | 124/124 passed, 0 failed, 0 skipped (the 30-second volume-session assertion unchanged and passing) |
| Analyze | warning-clean (0 warnings) |
| Authoritative ASan slice | 4/4 passed |
| strict OpenSpec | 91/91 |
| hygiene suite (status/contracts/visual/exact-tree) | PASS |
| unsigned release-candidate qualification | PASS |
| E2E (`dev.ps1 test-e2e`) | 8 passed / 0 failed / 0 skipped, `boundToHead=true` |
| UIA core | 16 passed / 0 skipped / 0 failed |
| UIA full | 23 passed / 6 skipped / 0 failed |
| visual catalog (`-Gallery`) | 20 recorded / 20 passed / 0 failed |
| focused many-tabs | 1/1 passed |

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

## H. Projection memory — REJECTED (recorded tradeoff)

1M-row remeasure: resident projection **520.5 bytes/row measured** (private
delta), estimator 404.4 B/row, `sizeof(ProjectionEntry)` 104 bytes as the
row floor, residual +116.1 MB (~22%) recorded as unseparated un-estimated
node/heap overhead (not asserted fragmentation). 8.2 found no accidental
duplication; the only large removable structure is the filename accelerator
the suite proves is used, and the spec rejects trading search latency for
bytes. No memory change retained; the pooled change additionally removed the
transient per-entry owning copies.

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

- `implementation_sha`: `63ddc21acf089ad92ddb5783882a74a595a2513c`.
- `certified_binary_sha`: the final documentation tip rebuilt before the
  ladder (all presets; identity binding passes).
- `evidence_sha` / `repository_tip_sha`: the final tracked tip after the
  report commit.
- `executable_impacting_digest` / verdict: recorded by the 11.6 equivalence
  gate run from `63ddc21acf089ad92ddb5783882a74a595a2513c` to the final tracked
  tip (PASS, digest 244d9c4d430059db..., 389 paths), with the live verdict in
  the PR body outside the checked commit.

## L. Hosted CI

`36999522143` (feature-branch push at `6bbd92d`) inspected once: six jobs,
zero executed steps → `HOSTED_CI = BLOCKED_EXTERNAL`. The final tip is
inspected once after the push and reported live; local results remain
labelled `LOCAL-PARITY-NOT-HOSTED-CI`.

## M. Remaining work

- External only: hosted CI execution (billing/infrastructure), production
  signing/pins, clean-host install, elevated MFT/USN, the raw-volume
  completion bound (predecessor 5.4 / residual 2.3 stay unchecked), the
  physical display matrix, and authorized guest/provider work.
- The temporary equivalence-control worktrees/branches are removed before
  the push; no synthetic commits exist on the feature branch.
