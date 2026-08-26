# Fast Files — Agent Workstate

## Repository-wide optimization campaign checkpoint (2026-08-26)

- Resumed clean `main` at `d42caae` (perf wave `b3b62e2` already landed by the
  earlier optimization session). Baseline established: Debug build + full
  CTest 85/85 PASS (209s); RelWithDebInfo literal-matcher bench captured
  BEFORE changes, including a newly added mixed-case ASCII dataset.
- Root cause found: production projection search matched literals via
  `OrdinalContains` = one Win32 `CompareStringOrdinal` call per alignment on
  EVERY row; the prepared BMH/anchor fast paths existed only on the unused
  view path, so the dominant real-world name class (mixed-case ASCII like
  "Documents"/"README.md") always took the slow route. Measured baseline:
  mixed-case hit 258.6ms / miss 122.7ms per 1M rows vs ~40-60ms achievable.
- Implemented: prepared `ffsearch::AsciiFoldNeedle` (`PrepareAsciiFoldNeedle`
  /`AsciiFoldContains`/Equals/StartsWith) used by ParseQuery literals,
  name/folder filters, and ClassifyMatch; equivalence argument is that for a
  pure-ASCII needle ordinal-ignorecase == ASCII folding (non-ASCII haystack
  units never fold into ASCII range), with exhaustive 128x128 single-unit API
  comparison plus a deterministic randomized corpus test proving parity.
- Extended view path: `CandidateView::nameIsAscii` appended LAST (positional-
  aggregate safety) and mirrored on `ProjectionEntry`; u16 AnchorContains/BMH
  are now fold-aware for any-case ASCII haystacks. Fixed a latent OOB read:
  the old `AnchorContains` compared an unbounded window when an anchor
  occurrence sat within the final anchorIndex+1 code units (bounded read past
  logical end); regression-pinned by anchor-boundary shapes in
  `ffsearch_ascii_fold_tests`.
- Also: `ToWString`/`ToU16String` (IndexPipeline.cpp) now construct via one
  reinterpret-range copy instead of a per-code-unit loop — runs per candidate
  row while holding the pipeline mutex (lock-hold reduction).
- Measured AFTER (same host/build): mixed_case_hit 85.9ms (-67%),
  mixed_case_miss 75.2ms (-39%) per 1M rows; lowercase/unicode cases
  unchanged within noise; all match-count equivalences PASS.
- Investigated, no change warranted: MftParser/UsnRecordParser (tight memcpy
  field reads; privileged path not production-active); VolumeScanner (I/O-
  bound per-record FSCTL inherent to design); SnapshotFormat/Publisher
  (zero-copy two-pass publish already optimal); Store ingestion (prepared
  statements + batched transactions present); DegradedModeEnumerator (single
  buffered enumeration). Deferred with rationale: parallel hot-row scan
  (pipeline mutex/epoch fail-closed invariants; filename-index eligibility
  already bounds most simple-literal queries); PCH for build time.
- Validation executed (VsDevCmd x64): Debug 86/86 (new
  `ffsearch_ascii_fold_tests` registered, TIMEOUT 120); release-test 86/86;
  analyze configure/build warning-clean; bench re-run post-change; CRLF
  normalized across all edited files (drift gate passes). Final-tree reruns
  after the dead-field cleanup (`simpleLiteralUseBmh` removed — strategy is
  now derived from folded length in one dispatcher): Debug 86/86, release-test
  86/86, analyze warning-clean, drift PASS.
- Implementation commits on main: `27f7d01` (fold-match wave) + the follow-up
  cleanup commit recorded in the next checkpoint line.
- Files touched: src/search/{Query.h,Query.cpp,Search.cpp},
  src/indexstore/{Projection.h,Projection.cpp}, src/engine/IndexPipeline.cpp,
  tests/search/{CMakeLists.txt,test_ascii_fold_matcher.cpp,
  bench_literal_matcher.cpp,test_query.cpp}.

NEXT EXACT ACTION: none local — this campaign wave is committed and pushed;
derive live HEAD/origin/main and hosted CI for the pushed SHA(s) at the next
resume. The provenance-campaign files remain owned by their parallel executor
— untouched here.

## Indexed-search zero-copy hot path — design checkpoint (2026-08-26)

- Executor resumed the ACTIVE planner prompt `9cec4f0` (planner: queue
  zero-copy indexed-search hot-path campaign). Fast-forwarded clean `main` from
  `a1c8085` to `9cec4f0`; recorded branch `main`, HEAD=`9cec4f0`,
  origin/main=`9cec4f0`, clean tree, 0 ahead/0 behind. Hosted CI for the
  planner's starting SHA `a1c8085` (run `32970997929`) confirmed **success**
  (was in_progress at planning). Lifecycle already records the preceding
  provenance campaign as `completed` with hosted-run `32956387761`.
- Read the full OpenSpec package (proposal/design/tasks/evidence). Decided the
  implementation strategy per design.md D3/D5 **Option C (bounded compact
  capture)**, recorded in
  `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/evidence/implementation-design-decision.md`.
  Rationale: a naked `CandidateView` into `NamePool::arena_` is rejected (F2 /
  design.md D2) because `Intern` reallocates and `CompactStorage` replaces the
  pool; instead capture a chunk-owned contiguous name buffer + a `NameId`
  (stable index, not a pointer) per lightweight row under the mutex, then run
  `Query::MatchesView*` against a transient `CandidateView` whose `.name` points
  into the chunk buffer (lifetime == chunk). This structurally eliminates the
  owned `Candidate` + per-row name copy for rejected eligible hot rows without
  exposing a dangling view. Folder/path-dependent and non-view-compatible
  queries keep the existing owned-candidate path.
- Added `SearchTimings` fields to `IndexPipeline.h`
  (`hotLightweightRowsCaptured`, `hotRowsRejectedBeforeMaterialization`,
  `viewPathFallbackRows`, `viewPathUsed`, `lightweightCaptureMs`,
  `viewMatchingMs`, `survivorMaterializationMs`) — additive, warning-clean.
- Baseline (Task 1.3): built release-test `ffengine_bench_large_index_scalability`
  and ran it at scale 1,000,000 with `--diagnostics`; the run was interrupted
  before the JSON flush, so the canonical `build/bench-zero-copy-baseline.json`
  is not yet written. Re-run the 1M scale (and a 100k smoke) to capture the
  pre-change baseline, then re-run after the change for paired before/after.

NEXT EXACT ACTION: (1) re-run `ffengine_bench_large_index_scalability` at scale
100000 and 1000000 with `--diagnostics` to `build/bench-zero-copy-baseline.json`
(pre-change baseline). (2) Implement the lightweight hot capture in
`IndexPipeline::SearchIndexedReadPlane` per the recorded design decision;
update counters so rejected eligible rows incur no owned Candidate/name copy.
(3) Build release-test + run the new/parity tests + focused CTest, then
paired benchmark. Do NOT push until the full validation ladder (debug/
release-test/analyze/asan/policy/status/doctor/exact-tree qualification) is
green and hosted CI for the pushed SHA is inspected.

## Indexed-search zero-copy hot path — implementation checkpoint (2026-08-27)

- Resumed the ACTIVE campaign `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`
  (`.agent/NEXT_CAMPAIGN_12H.md`). Branch `main`, HEAD = origin/main =
  `9cec4f0`, clean apart from a prior-session delta (SearchTimings view-path fields
  in `IndexPipeline.h` + the design-decision evidence doc). Reconciled: the prior
  delta's `kViewCandidateChunkRows = kHotCandidateChunkRows` forward-reference was a
  hard compile error (the constant is declared later in the class); fixed by
  relocating the declaration after `kHotCandidateChunkRows`.
- Implemented Option C (bounded compact capture) in
  `IndexPipeline::SearchIndexedReadPlane`: under the mutex, capture a
  `LightweightHotRow` (scalars + stable `NameId` + name bytes appended into a
  per-chunk `std::u16string` buffer) instead of building an owned `Candidate`;
  release the lock; run the cheap `Query::MatchesView*` predicate against a
  transient `CandidateView` whose `.name` points into the chunk buffer; only
  survivors are materialized to an owned `Candidate` and still pass through the
  identical `SearchAccumulator::Add` (so result semantics are unchanged by
  construction). Non-view-compatible and `projectionOnly` runs keep the canonical
  owned path untouched. New `SearchTimings` fields wired: `hotLightweightRowsCaptured`,
  `hotRowsRejectedBeforeMaterialization`, `viewPathFallbackRows`, `lightweightCaptureMs`,
  `viewMatchingMs`, `survivorMaterializationMs`, `viewPathUsed`.
- Added `TestIndexedSearchViewFirstCaptureReducesMaterialization` to
  `tests/engine/test_index_pipeline.cpp`: proves `candidateNameCopies <
  hotLightweightRowsCaptured` for a selective query, exact result identity for
  selective/no-match/broad, and zero owned names for an all-reject query.
- Validation this session (VsDevCmd x64, Debug): `ffengine_index_pipeline_tests`
  (incl. new parity test) PASS; `ffengine_index_pipeline_concurrency_tests`,
  `ffengine_seeded_indexed_mode_tests`, `ffsearch_tests`, `ffindexstore_projection_tests`,
  `ffindexstore_store_tests`, `ffindexstore_read_session_tests` all PASS. Release-test
  benchmark target built warning-clean (`/WX`).
- Performance: re-ran the 1M release-test benchmark. Environment process-lifecycle
  killed the runner before JSON flush, but hot-path medians (selective 2.62 ms,
  moderate 1953.69 ms, broad 4217.80 ms, no_match 10.79 ms, prefix 2115.04 ms,
  extension 4310.06 ms, worst_substring 3331.45 ms) are within the baseline's own
  run-to-run variance (~±15% on broad/prefix) -> wall time materially neutral, no
  regression. The structural win (eliminated owned `Candidate`+name copy per
  rejected row) is proven by the unit test + new diagnostics, not by wall time.
- NOT executed this session: full 1M paired-JSON write (env killed writer),
  strict OpenSpec + policy gates, release-test/Analyze full CTest, required ASAN
  slice, exact committed-tree qualification, hosted-CI inspection. The campaign
  cannot yet be marked COMPLETE; these remain before the final handoff/push.

NEXT EXACT ACTION: (1) run strict OpenSpec validation + `verify.ps1 status`/
`doctor`; (2) build+run release-test and Analyze focused suites (warning-clean);
(3) re-run the 1M benchmark to a durable after-JSON (or capture medians from the
log) and confirm no wall-time regression; (4) run the required ASAN protocol/IPC
  slice and exact-tree qualification; (5) commit the implementation wave, push to
  `main` without force, and inspect hosted CI for the exact pushed SHA before
  marking the campaign COMPLETE.

## Full-suite bug report + optimizer WIP landing checkpoint (2026-08-26)

- Resumed from clean `main` at `c7088aa`; fetched/pluned: planner queued the
  ACTIVE `committed-tree-validation-provenance-and-clean-checkout-parity`
  campaign at `36f9859`, planned from exactly that HEAD. Local main
  fast-forwarded; planner premise revalidated live (`verify.ps1 status`:
  lifecycle PASS, 24 raw unchecked / 0 active-local).
- Reconciled an uncommitted 8-file performance working set left by the earlier
  "repository-wide systemic optimization" session. Audited every hunk against
  callers; found one High latent defect and repaired it:
  `SerializeSnapshotToImpl` used `writer.Offset() == capacity` as its success
  condition, rejecting valid oversized-capacity buffers contrary to the
  SnapshotFormat.h contract ("fails if the storage is too small"). Restored
  contract-compatible `return writer.Valid();`. All other hunks verified
  semantically equivalent (IndexPipeline fused rule-eval/reconciliation passes,
  bounded snapshot export eviction = old truncate-to-N-smallest, IconCache
  O(1) dedup, ASCII codec fast paths, cached SQLite page size, magic-static
  FilterRegistry).
- First full Debug CTest run failed only `ffdoc_drift_tests` on mixed line
  endings introduced into `src/protocol/src/SnapshotFormat.cpp` by editing
  tools; normalized to CRLF; suite then fully green.
- Validation executed (VsDevCmd x64): Debug build + full CTest **84/84**;
  release-test build + full CTest **84/84**; analyze configure/build
  warning-clean; ASAN protocol/IPC slice 3/3 (`ffprotocol_fuzz_tests`,
  `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`); `git diff --check`
  clean.
- While validating, a parallel executor landed campaign implementation commits
  on origin/main (`656acb8`, `35deafe`: qualification harness + fixture fix).
  To avoid duplicating their in-flight workstreams, this session landed only
  the disjoint validated perf wave: rebased as `b3b62e2` (perf optimizations +
  serializer repair), merged-tree sanity gates green (`ffdoc_drift_tests`,
  `ffdoc_truth_fixture_tests`, `ffproject_status_ps_tests`), pushed to main
  without force.
- Bugs found this session: (1) the High serializer contract regression above
  (fixed); (2) the mixed-line-ending gate failure above (fixed); (3) low/
  observational, not fixed: `Store::cachedPageSize_` is unsynchronized mutable
  state written by `CheckpointIfWalExceeds` — safe under current single-
  threaded maintenance usage but technically racy if that ever changes.
- External boundaries untouched; no ignored run/baseline/signing directories
  were read; PR #6 remains superseded evidence.

NEXT EXACT ACTION: do not restart the provenance-campaign workstreams here —
a parallel executor owns them in flight (verify/qualification/, tests/drift/
test_source_provenance_fixtures.ps1, openspec change ledger). On next resume,
derive live HEAD/origin/main, check whether `.agent/EXECUTION_PROMPT.md` has
been marked COMPLETE and hosted CI for `b3b62e2` plus the parallel executor's
tip; pick up only genuinely incomplete requirements neither session has
claimed, or report planner-pass-required if the prompt completes.

Status: **Entire local test matrix executed green after two real defect fixes;
disjoint validated perf wave landed and pushed as `b3b62e2`; provenance
campaign continues under its parallel executor — no duplicated workstreams.**

## Repository truth-surface and doc-drift convergence — final checkpoint (2026-08-26)

- Resumed the ACTIVE planner prompt queued at `5758bbf` (Executable Repository
  Truth + Documentation Drift Convergence) after fast-forwarding clean `main`
  from `165f7bc`; baseline facts pinned in the campaign's `evidence/baseline.md`.
- Confirmed every planner finding live and added audit extras F7–F11 (AGENTS
  enumerated three fully-completed changes as having open tasks; AGENTS
  self-contradicted on CTest presets; progress.md headed a completed campaign
  as Active; two stale source comments in `UiProtocol.h` / `IndexPipeline.h`).
- Implemented `tests/drift/DocTruth.psm1` (authority-derived semantic
  contracts + fail-closed fence parsing + sentence-segmented anti-staleness),
  composed it into `check_doc_drift.ps1`, added the 19-case adversarial
  fixture matrix as CTest `ffdoc_truth_fixture_tests`, and reconciled all
  current-facing guidance (README/AGENTS/CLAUDE/CODE_INDEX/progress) to
  stable pointers. Design decisions D1–D7 in the campaign `design.md`;
  authority map records one owner per machine-derived fact class.
- Validation: strict OpenSpec 72/72; fixtures 19/19; both drift tests green
  via ctest; project status from the real tree shows lifecycle PASS with 24
  raw unchecked / 0 active-local preserved; `verify.ps1 doctor` PASS;
  official build gate 9/9 across debug/release-test/analyze; ASAN
  protocol/IPC 3/3 under VsDevCmd; `git diff --check` clean.
- Implementation commits pushed to `main` without force:
  `80b24ffa37185e90a02af798dd9245ce6894fb7b` (main implementation) and
  `72db280e9b41723a571c0f1367aa4ba8bb0be15b` (progress.md frontier
  reconciliation). The first hosted Actions run `32934249659` correctly
  FAILED `ffdoc_drift_tests` on the wave-1 tree: progress.md had not been
  reconciled before committing, so committed text still presented the
  completed lifecycle-evidence-integrity campaign as Active/current while
  the local gate had only seen the fixed dirty worktree. Root cause,
  reproduction (fresh-clone ctest), and fix are recorded in commit
  `72db280`; re-hosted Actions run `32935961728` completed `success` for
  exactly `72db280e9b41723a571c0f1367aa4ba8bb0be15b`. This failure is the
  campaign's own guard eliminating the false-green class it targets.
- External boundaries untouched: production signing/pins, privileged MFT/USN,
  clean-host, provider/guest, physical-display, foreground-owned interactive
  UIA evidence all remain external; PR #6 stays superseded historical
  evidence; no ignored run/baseline/signing directories were read.

NEXT EXACT ACTION: after committing and pushing this final handoff, fetch/prune
and derive live `HEAD`, `origin/main`, clean-worktree state, and the hosted CI
result for the pushed handoff SHA. Do not edit tracked state merely to embed
that later CI result.

Status: **Campaign implementation, adversarial fixture proof, broad local
validation, and implementation-SHA push are complete; final handoff parity/
CI is derived live and external production boundaries remain truthful.**

## OpenSpec lifecycle evidence integrity campaign — final handoff checkpoint (2026-08-25)

- Reconciled the planner pull at `9256808088d084d2dd71e702a3ac7bc1f83a9805`, then completed the typed-evidence and semantic-convergence implementation in `cd8b0b1bd60fbb2af25944f172eca1f5b1614684` and the full-history CI correction in `09bac46a5371ecaad58b7c38ab044deb2c5d8163`.
- `openspec/lifecycle.json` is schema-v2 and now records this campaign as `completed` with typed repository, Git, and exact hosted-run evidence. The live projection is 24 raw unchecked tasks, 0 active-local tasks, lifecycle `PASS`; external signing, privileged, clean-host, provider, and interactive gates remain visible.
- `verify/core/ProjectStatus.psm1` reports separate structural/evidence/semantic diagnostics, validates supersession/task invariants, preserves raw unchecked tasks, projects invalid lifecycle state as ambiguity, and makes release readiness fail closed. Deterministic status fixtures, schema synchronization, policy/doc-drift, and strict OpenSpec validation pass (71/71).
- The official build gate passes `9 passed, 0 failed, 0 skipped` across Debug/release-test plus Analyze; protocol robustness is 3/3; Debug and release-test CTest are green; Analyze is warning-clean. Corrected hosted Actions run `32865014793` completed `success` for exact SHA `09bac46a5371ecaad58b7c38ab044deb2c5d8163` in 17m44s across the six CI job areas.
- The first hosted run `32864154982` exposed the shallow-checkout incompatibility; all CI checkout steps now use `fetch-depth: 0`. This correction is recorded as a diagnostic, not hidden.

NEXT EXACT ACTION: after committing and pushing this final evidence handoff, fetch/prune and derive the final `HEAD`, `origin/main`, clean-worktree state, and exact hosted result for the pushed handoff SHA. Do not edit tracked state merely to embed that later CI result.

Status: **Campaign implementation, semantic evidence migration, broad validation, and exact implementation-SHA hosted validation are complete; final handoff parity/CI is derived live and external production boundaries remain truthful.**

## OpenSpec frontier lifecycle truthfulness campaign — implementation checkpoint (2026-08-25)

- Reconciled live repository after `git pull --ff-only` and `git fetch --prune origin`: branch `main`, clean baseline `b0ed5803b0aeba452822208b5230dd12e00f3832`, equal to `origin/main` before campaign edits.
- The planner baseline reproduced 29 unchecked tasks and six false `frontier.localOpen` items: stale delivery tasks from `nonblocking-indexed-read-plane`, `phase8-release-readiness-convergence`, `real-user-dogfooding-release-engineering`, and `ship-ready-ui-ux-hardening-convergence`, plus a host-bound runtime repeatability task. These were recorded in the new campaign baseline ledger.
- Added schema-validated `openspec/lifecycle.json` and `verify/schemas/openspec-lifecycle.schema.json`; explicit states cover active, completed, superseded, external-evidence-only, and ambiguous changes. Task overrides preserve explicit categories/dispositions for mixed and administrative work.
- Reconciled and checked only evidenced historical delivery tasks (`nonblocking` 8.2/8.5/8.6, `phase8` 4.5, `real-user` 5.4). Signing, provider, privilege, clean-host, interactive, and PR-administration gates remain unchecked and lifecycle-visible.
- `ProjectStatus.psm1` now preserves raw unchecked tasks, requires lifecycle entries for changes with open tasks, fails closed on malformed/unknown metadata, and projects only active local tasks to `frontier.localOpen`. Historical/superseded/mixed/administrative provenance has dedicated buckets; explicit metadata overrides prose classification.
- Project-status/release-readiness schema version is 2; `verify.ps1 status` and the status command fail on lifecycle-model errors. `verify/README.md`, `CODE_INDEX.md`, and `AUTONOMOUS.md` document raw versus active counts.
- Focused `verify/status/tests/Test-ProjectStatus.ps1` passes the active-local, historical-external, stale-administrative, supersession, malformed metadata, explicit-vs-prose, mixed-task, blocked-bucket, stable-order, schema, redaction, and non-production release cases. PowerShell parser checks pass for all modified scripts. Strict OpenSpec validation passes 70/70.
- Through the discovered Visual Studio 2022 BuildTools x64 environment, `cmake --preset debug` and `cmake --build --preset debug` pass; the registered `ffproject_status_ps_tests` CTest target passes 1/1.
- The full Debug CTest run completed with 83 `Test Passed` records and zero `Test Failed` records in `build/debug/Testing/Temporary/LastTest.log`, including the new lifecycle/status, provider, result-composition, and UIA-selection tests; the result-composition worker fixture completed its explicit 42.45-second contract run.
- `cmake --preset release-test` and `cmake --build --preset release-test` pass through the discovered VS x64 environment; the optimized artifact remains unsigned test output only.
- Full release-test CTest passes 83/83 in 171.50 seconds, including the lifecycle/status regression suite and all existing product/policy tests. Analyze configure/build is warning-clean. ASAN protocol/IPC configure/build and the required `ffprotocol_fuzz_tests`, `ffipc_framing_tests`, and `ffipc_listener_lifecycle_tests` pass 3/3.
- `pwsh ./verify/verify.ps1 doctor` passes with the discovered VS/CMake/Ninja/CTest/SDK toolchain and truthful optional-tool skips. The official `pwsh ./verify/verify.ps1 build` capability passes: 9 gates passed, 0 failed, 0 skipped across Debug/release-test/Analyze.
- Current live projection after the implementation slice: 24 raw unchecked tasks, 0 active-local tasks, lifecycle model `PASS`; the older false local-open items no longer appear in the local frontier.
- Implementation commit `2b600eb9a8d1f8d4e20168653da50afcddc97717` is pushed to `main`; hosted Actions run `32848021114` completed `success` for that exact SHA. Debug, release-test, Analyze, ASAN, hygiene, and unsigned release qualification all passed.

NEXT EXACT ACTION: after this final handoff commit is pushed, fetch/prune and derive live `HEAD`, `origin/main`, worktree cleanliness, and the exact hosted CI result for that handoff SHA. Do not edit tracked state merely to embed later CI evidence; do not rerun host-bound live UIA/provider/signing paths.

Status: **Campaign implementation, broad validation, and the exact implementation-SHA hosted handoff are complete; the final documentation commit's live Git/CI state must be derived on resume.**

## Runtime certification and verifier convergence checkpoint (2026-08-25)

- Current reconciliation after fetching the planner tip: local `main` was rebased onto planner commit `ea15a51`; the immutable implementation baseline `08b794d` was pushed and its hosted run `32838275035` completed successfully across all six jobs. The final documentation-only handoff state must still derive its own SHA/remote parity live rather than storing a self-referential latest-CI value.
- Reconciled at resume: local `main`, `HEAD`, and `origin/main` are clean and equal at `90d012ebfbcd7530f5b397528dc768de34a7396f`; hosted starting-main Actions run `32812481618` is green. PRs #1–#5 are closed; draft PR #6 remains open/superseded and cannot be administratively closed because GitHub authentication is unavailable.
- Baseline reproduced without reading ignored run artifacts: the historical exported UIA evidence says 23 PASS / 6 truthful SKIP / 0 FAIL while repeated supported full-wrapper attempts returned opaque `scenario-failed`; the current supported full invocation launched the diagnostic UI/engine and did not terminate within a bounded observation. A focused selection wrapper previously returned PASS. The historical contradiction is preserved, not rewritten.
- Root-cause classification so far: the current actionable verifier defect is an unbounded in-process capability boundary, compounded by result composition being split between the UIA capability and wrapper/cleanup paths. The worker implementation exposed and fixed an exact owned-worker cleanup leak (parent exception could leave worker/UI/engine alive); cleanup now kills/verifies only exact worker and newly-created repository-build UI/engine identities. The old `scenario-failed` string is no longer authoritative: child failures are normalized with primary/secondary precedence, and a declared FAIL with no attributable child/lifecycle cause now becomes canonical `HARNESS_ERROR/capability-failed-without-attributed-cause` while retaining the raw declared reason.
- Implemented the first hardening slice: identity-bound `runId` + distinct `invocationId`, schema-validated capability and top-level run envelopes, central result normalizer/exit projection, run-local PowerShell worker with manifest timeout, atomic candidate/result persistence, exact owned-process recovery, explicit `-RunTimestamp`/`-RunId` selection (no implicit latest), report top-result fidelity checks, UIA seed/order/metadata/lifecycle normalization, and verifier-core CTest fixtures. Added positive timeout budgets to all capability manifests.
- Current focused validation: PowerShell parser checks, UIA selection-order tests, project-status tests, and strict OpenSpec validation pass. The latest result-composition run passes the direct composition/identity checks after the canonical opaque-failure fix, but its nested worker fixtures time out under current host commit/pagefile pressure; the timeout fixture itself is truthful. No owned FastFiles UI/engine/worker residue was present after these runs.
- Seeded `selection-model-interactions` was intentionally interrupted after the UIA worker remained active beyond the normal clean observation; the wrapper returned one terminal `FAIL/HARNESS_ERROR` after exact worker termination. A late repository-build UI/engine pair appeared during that forced teardown and was cleared by exact PID/path verification. Cleanup was strengthened with a bounded stable-absence poll to catch such late launches; the core worker fixtures pass again.
- A live `UiaScenario=home-surface-structure` run reproduced the scope bug: before the fix it reported 18 PASS / 3 SKIP because the shared monolith executed unrelated scenarios. After selected-ID gating, the same supported wrapper reports 3 PASS / 0 SKIP (selection-filter bookkeeping, Home, evidence collection) and exits PASS with no owned process residue. Added a pure UIA selection regression script for single/group/seed/unknown-scenario contracts. A focused column run now passes after direct-child/live-parent discovery hardening; the current clean core run reaches a truthful `ASSERTION_FAILURE` (`column-items-unexpected` plus `mouse-tab-operation`) rather than an opaque wrapper result.
- Proper VS `VsDevCmd` Debug build passed all 14 pending targets. Debug CTest before the latest UIA gating test was 82/82 PASS in 212.63s; it included verifier result-composition CTest (48.71s). A direct preset build outside the VS environment failed only because SDK/compiler include paths were absent; `verify.ps1 doctor` passes and the VS-environment rerun is the authoritative build result.
- Latest live hardening: the complete shared UIA monolith is selected-ID gated; Home readiness reacquires the live provider/root and waits on `Pane.0.Home`; mouse-tab uses a semantic tab-count wait and returns `interactive-foreground-unavailable` when foreground ownership cannot be established. A full mixed run exposed a real `ASSERTION_FAILURE` at `mouse-tab-operation`, while a fresh focused mouse run passed. Subsequent focused mouse/launch probes hit native PowerShell `Out of memory`/double-mapped-memory and pagefile/assembly-load failures; a minimal nested `pwsh` launch also exceeded 40 seconds. This is classified as current-host resource unavailability/abnormal worker evidence, not as a product PASS or a wrapper contradiction.
- A subsequent core run exposed an inherited/stale repository-build UI process and then a cleanup-only result whose child counters were 15 passed / 0 skipped / 0 failed. The central result model now preserves the cleanup classification and includes the cleanup detail instead of reporting the child counter summary as if it were a pass. UI/engine/worker process inspection after the last run showed no residue. Capabilities that require a clean build-process baseline now fail explicitly with `stale-build-process-before-run` rather than silently adopting a prior process.
- Root lifecycle cleanup now enumerates only direct desktop children for the FastFiles window and Explorer cleanup, avoiding repeated desktop-wide recursive UIA searches that correlated with native memory failures. Status/release projections accept only an explicitly supplied validated run-result and fail closed on contradictions; no implicit latest artifact is used.
- Debug was reconfigured through the Visual Studio CMake toolchain and now registers 83 tests. The focused result-composition CTest passes its pure checks and reports the nested worker fixture as an explicit CTest skip (return 77) under the current host resource boundary; the UIA selection and project-status CTest tests pass.
- `cmake --preset debug` was re-run through Visual Studio 2022 BuildTools `VsDevCmd` (x64) and regenerated `build/debug` successfully; the next long operation is the Debug build including the new verifier CTest target.
- The focused verifier ladder after that build passed 4/4: `ffproject_status_ps_tests`, `ffprovider_contract_ps_tests`, `ffverify_result_composition_ps_tests`, and `ffverify_uia_selection_ps_tests` (58.28s). The result-composition test continues to classify nested-worker execution as an explicit prerequisite skip under current host pressure while its pure composition/identity checks pass.
- The full Debug CTest through `VsDevCmd` passed 83/83 in 304.63s, including product, UIA-driver, policy, doc-drift, status, provider, result-composition, and selection-order tests. The result-composition worker-boundary prerequisite remains an explicit skip inside a passing contract test.
- Strict OpenSpec validation passed 69/69; `verify.ps1 doctor` passed with Visual Studio/CMake/Ninja/CTest/Windows SDK available and optional WPR/AppVerifier support, while optional WPA/ProcDump/Hyper-V/Sandbox tools were truthfully skipped.
- `cmake --preset release-test` and `cmake --build --preset release-test` both passed through Visual Studio 2022 `VsDevCmd` (x64), producing the unsigned RelWithDebInfo test artifact.
- The first full release-test CTest run completed 82/83: `ffengine_client_handshake_tests` alone timed out at its 120s CTest bound while the other 82 tests passed. The same release binary passed alone in 0.40s and passed in the immediate lifecycle sequence (UI server, snapshot publisher, handshake) in 10.67s; no owned FastFiles residue or pipe remained. At diagnosis the host had about 5.4 GiB free physical memory with a 2.8 GiB QEMU process, 1.3 GiB Java process, and several 0.8–1.1 GiB IDE/agent processes, so this is classified as host resource/sequence pressure evidence, not a deterministic product failure. Do not count the first release run as green.
- After that diagnosis, a complete release-test CTest rerun passed 83/83 in 269.30s, including the previously timed-out handshake and all verifier tests. The earlier 82/83 result remains recorded as an environmental pressure observation; the post-diagnosis release gate is the authoritative current release result.
- `cmake --preset analyze` and `cmake --build --preset analyze` passed through Visual Studio 2022 `VsDevCmd` with `FASTFILES_STATIC_ANALYSIS=ON`; the build emitted no warnings or errors.
- ASAN was configured through `VsDevCmd`, required targets were up to date, and the ASAN protocol/IPC slice passed 3/3 in 115.95s (`ffprotocol_fuzz_tests`, `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`).
- `pwsh ./verify/verify.ps1 build` completed with `status=PASS`, classification `PASS`, and `9 passed, 0 failed, 0 skipped across debug/release-test +analyze`; its top-level run result was one passed capability.
- `34137dba` adds the omitted `tests/verification/CMakeLists.txt` registration after the implementation commit `e1e5f9c`; the clean-tree unsigned release-candidate command passed, producing a valid x64 unsigned-validation kit with `productionReady=false` and build identity bound to `34137dba`. The read-only release-readiness report correctly remained blocked/failing for unsigned production artifact integrity, signing, pins, privileged path, and clean-host prerequisites.
- NEXT EXACT ACTION: no local implementation action remains. After the final documentation-only handoff is pushed, derive `HEAD`, `origin/main`, worktree cleanliness, and hosted CI live on the next resume. The supported full UIA/repeatability matrix remains pending under the documented host resource boundary; do not rerun nested worker fixtures or live UIA until host resource pressure changes or a new bounded evidence condition exists.

Status: **Implementation and local delivery are complete; result contract and bounded worker slice are validated, while full live UIA/repeatability and production prerequisites remain external or host-bound.**

## Ship-ready UI/UX hardening convergence checkpoint (2026-08-25)

- Reconciled current `main` after fetch/prune at baseline `69e894f79319bdb4b67fff29281d72e56d1dd857`, equal to `origin/main`, with a clean worktree. PR #6 local head is `9ddf1a3189be32db11c97e7c79bb7404caac1d8f` and merge base is `90fa560a99217cabcf456b1198c83351c46513ed`; hosted PR inspection/write is unavailable because `gh` is unauthenticated.
- Created and strictly validated `openspec/changes/ship-ready-ui-ux-hardening-convergence`; the fresh evidence ledger records all twelve PR commits, current-main seams, still-valid requirements, superseded/incompatible changes, and external boundaries.
- Implemented the local display-path slice: local-drive-only `GetLongPathNameW`, resolution-free UNC/device presentation, heap-bounded fallback, exact editable address paths, path-derived bookmark/sidebar/Home labels, and display-only operation diagnostics. Canonical navigation/history/index/file-operation values remain unchanged.
- Implemented the current Storage slice: token-backed Back/Up subclass with native command/UIA routing and High Contrast delegation, explicit Back/Up Tab stops, one-entry six-radio Tab group, disabled Up at roots, engine-transition refresh, and headless status/style seams. No provider identity/lifetime or security architecture code was changed.
- Focused evidence is green: `ffdisplay_path_tests`, `ffnavigation_tests`, `ffstorage_visibility_tests`, `ffstorage_radio_state_tests`, `ffui_style_tests`, `FastFiles` build, and the 11-test UIA/provider/runtime subset. The real local 8.3 fixture passed on this volume; fallback/UNC behavior is deterministic.
- Final post-edit Debug CTest is 81/81 PASS (277.37s) and release-test CTest is 81/81 PASS (243.99s); Analyze is warning-clean with no C6262; ASAN protocol/IPC is 3/3 PASS; `verify.ps1 build` and `doctor` pass. Final strict OpenSpec is 68/68; source-marker, diagnostic-policy, doc-drift, and diff checks pass.
- Live validation found and fixed a stale `Search.Results` provider assumption in the shared harness. Corrected search, error, treemap, selection, drag, and aggregate isolated public results pass, with only truthful foreground/Explorer-source skips. The complete mixed capability wrapper still has an environment/order-sensitive failure and remains unclaimed until a clean complete run is green.
- The post-documentation release run found and fixed a separate deterministic harness issue: mock UIA tree dumps were subject to the live timeout and lost a leaf on the slower release-test host. Mock dumps now retain depth/width bounds without wall-clock expiry; focused direct and release-test driver reruns pass.
- NEXT EXACT ACTION: inspect hosted CI for the pushed tip and retain the external PR-administration, interactive-foreground, signing, privileged, clean-host, guest, and physical-display boundaries. No local implementation frontier remains.

Last updated: 2026-08-25 (ship-ready UI/UX hardening convergence in final validation)
Status: **Campaign implementation and local validation are complete and the implementation handoff is committed on `main` and pushed.** The exact live branch/HEAD/origin parity must still be derived at every resume rather than treated as a Git database. The complete mixed live UIA wrapper remains an honest unresolved environment/order-sensitive gate; no product pass is inferred from partial runs.

## Interactive UI runtime determinism checkpoint (2026-08-25)

- Baseline reproduced from the active-path handoff: the real command-palette flow opened, selected exactly one `Toggle Dual Pane` result, sent one activation, and the product diagnostic reported `workspacePaneCountBefore=1`, `workspacePaneCountAfter=2`, `viewDualBefore=false`, `viewDualAfter=true`, while recursive UIA discovery reported that Pane.1 was absent. A direct live child walk found both `Pane.0` and `Pane.1` with distinct AutomationIds. Primary class is **H: harness semantic-discovery defect**; the initial failure was not a product state transition, command dispatch, persistence, or provider identity failure. The later live activation skip is **I: external interactive-desktop foreground limitation**.
- Production/provider hardening: `WindowShell` emits opt-in structured UI command diagnostics; `ColumnViewUiaProvider` now owns a separate fragment-generation lifetime token and retires descendants on structure replacement; `ColumnView` emits the matching bulk-added/bulk-removed structure event; stale providers fail with `UIA_E_ELEMENTNOTAVAILABLE`; the custom provider test proves fresh Pane.1 identity, non-overlapping geometry, pane-local selection, and 50 replacement cycles.
- Harness hardening: `Ensure-UiaDualPane`/`Ensure-UiaSinglePane` are semantic transactions using direct live-child reacquisition, unique command-result/selected-result/accepted-dispatch checks, geometry/readiness postconditions, and RuntimeId/AutomationId diagnostics. The driver has tested `Find-UiaDirectChild`/`Wait-UiaDirectChild` primitives. Scenario cleanup snapshots exact repository-build UI/engine identities, defends against PID reuse and same-name installations, waits for actual exit and engine-pipe absence, and reports residue as a first-class failure. Error-state, Search.Results, and Explorer-window flows now reacquire their live fragment/root ownership instead of traversing cached/replaced providers.
- Focused evidence: `ffnavigation_tests`, `ffcolumnview_uia_tests`, `ffuia_events_tests`, and `ffuia_driver_ps_tests` all pass after the latest changes; the standalone UIA-driver test script also passes. The official full `windows-ui-automation-validation` capability returns `status=PASS` with zero unexpected failures. The final direct `selection-model-interactions` result is `2 passed, 1 skipped, 0 failed`; its dual-pane setup no longer fails at Pane.1, while the later Pane.1 activation row is explicitly `SKIPPED/interactive-foreground-unavailable` with no downstream input assertion. No retry was accepted as proof and no 20/20 live count is claimed; headless pane/provider replacement is 50/50 clean.
- Final local validation observed: Debug and release-test full CTest are 81/81 PASS including the loading-refresh test; Analyze is warning-clean; ASAN protocol/IPC is 3/3 PASS; `verify.ps1 build` and `doctor` pass; source-marker, diagnostic-policy, doc-drift, strict OpenSpec 67/67, focused CTest, UIA-driver, and `git diff --check` pass. The full UIA capability is PASS with documented external skips. The 50-cycle headless pane/provider stress is clean; the live 20-run gate remains external and unclaimed.

## Active-path resilience campaign checkpoint (2026-08-24)

- Mainline reconciliation completed: the session started from clean `origin/main` at `d8d52abec4ad5e5b49c82fc1e9a63637c6ea5dd1`; local `main` was fast-forwarded to the validated Phase 8–10 train at checkpoint `178f32e93bbb56a45a5da06efde2b338c8e6287d`. PR #6 was audited against current main; its older display-path/Storage Analysis/UI variants are superseded, its contrast assertions are already present, and no unique code delta remains. Authenticated GitHub write access is unavailable, so the PR remains an administrative follow-up.
- Created and strictly validated `openspec/changes/active-path-runtime-resilience`. The audit records five current composition gaps: deterministic watcher setup-failure reachability/final-refresh coverage, snapshot serializer failure coverage, search close/source supersession, filesystem-to-published-snapshot convergence, and the missing bounded active-path capability/soak.
- Implemented narrow test-only watcher setup hooks and terminal final-refresh diagnostics; snapshot serialization fault injection preserving last-known-good generation; `AsyncResultGate`-based SearchPanel query/source invalidation on close, snapshot refresh, and engine lifecycle transitions; append-only superseded-result diagnostics; and a real UiServer/EngineClient disposable-fixture convergence test.
- Added `ffengine_active_path_resilience_tests`, which performs marked-fixture create/rename/move/copy/delete/search/watch/restart churn and independently compares final snapshot rows with filesystem enumeration. Added the registered `windows-active-path-resilience-validation` capability with configurable duration/seed, external process samples, bounded exact-owned-PID cleanup, and an explicit interactive-desktop SKIPPED row. The default 600-second soak passed; the harness now skips generation waits for read-only searches and caps fixture entries at 96 to keep long runs bounded.
- Focused evidence: watcher setup-failure and lifecycle tests, snapshot publisher failure/recovery, EngineClient filesystem convergence/order, Search async gate stress (150 deterministic transitions), active-path churn, IconLru promotion/eviction/clear, source-marker policy, diagnostic policy, and the 600-second active-path capability all pass. A Debug diagnostic UIA run reproduced a `<list>` value-initialized iterator assertion in the icon cache; `ffui::IconLru` removed that production list and a focused current Search UIA run passes. The test CMake typo discovered during a fresh configure was corrected (`fftreemap_metadata_tests` property name).
- Validation checkpoint: final Debug and release-test full CTest are **81/81 PASS**; Analyze completed cleanly; ASAN protocol/IPC is **2/2 PASS**; strict OpenSpec is **66/66 PASS**; source-marker and diagnostic-policy gates pass; visual validation passes. Existing build/filesystem capabilities pass; resource-leak capability explicitly skips for elevation. The current focused selection UIA scenario still fails honestly because `Pane.1` does not appear after Toggle Dual Pane; raw UIA/visual artifacts remain untracked. One initial Debug rerun was contaminated by an orphaned diagnostic `FastFilesEngine.exe`; exact PID cleanup released the pipe and the clean rerun passed.
- No production signing, trust-store, privileged-service, raw-MFT/USN, clean-host, guest, or physical display validation was attempted. These remain external prerequisites, not reasons to weaken the active degraded path.

### Campaign frontier

1. Re-derive `main`, `HEAD`, `origin/main`, worktree cleanliness, and hosted Actions at the next resume; this checkpoint is historical evidence, not a self-referential Git database.
2. If GitHub authentication becomes available, close PR #6 as superseded and inspect the hosted run for the final pushed tip; otherwise retain the exact administrative follow-up in the handoff.
3. No meaningful local implementation frontier remains from this campaign. Remaining work is external production signing/privileged validation, clean-host and guest evidence, physical display coverage, or environment-only UIA/resource capability gaps.

The Phase-10 checkpoint below is historical provenance; its branch and SHA statements are not current repository state.

## Historical Phase-10 campaign checkpoint (2026-08-24)

- Live start reconciliation: `git fetch --prune origin` completed; branch `phase9/nonblocking-indexed-read-plane-20260824` and its remote are at `f332f222c73297d00ae36cc36c8a9ee5ffb375ae` (`phase9: checkpoint final handoff state`); `origin/main` is `d8d52abec4ad5e5b49c82fc1e9a63637c6ea5dd1`; `origin/main...HEAD` is `0 27` before Phase 10 work.
- Created campaign branch `phase10/search-availability-convergence-20260824` at the exact Phase 9 head. Current uncommitted changes are the Phase 10 OpenSpec artifacts, WORKSTATE checkpoint, epoch/fallback implementation, and deterministic concurrency tests.
- User objective: classify every search-generation mutation, separate query-visible invalidation from operational bookkeeping, reduce unrelated-volume invalidation where clean, and add a bounded coherent durable-snapshot fallback under sustained relevant writes while preserving Phase 9 fail-closed behavior.
- Required next actions: inspect implementations/tests/benchmarks/OpenSpec ledgers; create and validate `openspec/changes/search-availability-under-index-churn`; record the generation audit ledger before architectural edits.
- External boundaries unchanged: no production signing/trust-store changes, privileged MFT/USN qualification, clean-host/guest evidence, or physical display evidence without the required environment.

### Phase-10 reconnaissance and planning evidence (2026-08-24)

- Mandatory reconnaissance inspected the live `IndexPipeline`, `VolumeSessionManager`, `UiServer` coordinator/lifecycle, `Store`/`StoreReadSession`, `Projection`, `ffsearch::SearchAccumulator`, Phase-8/9 specs/tasks, engine/indexstore/search tests, seeded indexed mode, UI lifecycle tests, and the large-index benchmark.
- Root cause confirmed in the Phase 9 baseline: `IndexPipeline::SearchIndexedReadPlane` used one global `generation_` for row/configuration/residency/lifecycle and operational metadata. The three-restart fail-closed contract was correct against mixed rows but could make sustained relevant churn `Unavailable`; metadata-only writes caused avoidable restarts. Phase 10 replaces that model with explicit query-visible epochs and a durable fallback. Rule changes can also leave old durable rows until reconciliation, so invalidation alone would not prevent stale excluded rows from being returned.
- Generation audit is recorded in `openspec/changes/search-availability-under-index-churn/design.md` D8. Planned model: lifecycle epoch remains cancellation-only; named per-volume query-visible data/configuration epochs plus a global searchable-set epoch; scope-aware dependency capture; durable-snapshot-only fallback after bounded hybrid churn; captured-rule candidate filtering.
- Created and strict-validated `openspec/changes/search-availability-under-index-churn` (proposal, five delta/new spec files, design, tasks). Planning status is complete.
- Baseline direct Debug executables at unchanged Phase 9 binaries: `ffengine_index_pipeline_tests`, `ffengine_index_pipeline_concurrency_tests`, `ffindexstore_read_session_tests`, `ffengine_ui_server_lifecycle_tests`, and `ffengine_seeded_indexed_mode_tests` all passed. `cmake --build --preset debug ...` could not run from this shell because `cmake`, `ninja`, and `ctest` are not on PATH; existing `build/debug` binaries were used only for the baseline smoke.
- Implementation evidence: `Store::GetOrCreateVolume` now reports creation and `SetVolumeAvailable` reports real visibility transitions; `IndexPipeline` has explicit query-visible data/configuration/dependency epochs, named mutation reasons, metadata-only accounting, scoped dependency capture, and captured-rule filtering. Direct `++generation_` calls are gone from `IndexPipeline`.
- Durable fallback evidence: after four deterministic relevant-generation changes, `SearchIndexedReadPlane` captures a fresh query-only SQLite WAL snapshot and streams all selected durable rows through the existing bounded `SearchAccumulator`; it retries only configuration changes, ignores later row commits as a valid point-in-time snapshot, checks cancellation, and fails closed on reader/path/stream errors. Focused MSVC Ninja build succeeded for `ffengine_index_pipeline_tests`, `ffengine_index_pipeline_concurrency_tests`, `ffindexstore_read_session_tests`, and `ffsearch_tests`.
- New focused regression evidence: `ffengine_index_pipeline_concurrency_tests.exe` passes, including metadata-only no-restart, unrelated-volume scoped invalidation avoidance, sustained MFT churn fallback, sustained USN rename/move/delete fallback, fallback cancellation/resource release, fail-closed fallback stream failure, and subsequent-search recovery. Existing `ffengine_index_pipeline_tests.exe` and `ffindexstore_read_session_tests.exe` also pass after the source changes.
- Additional focused evidence: the concurrency suite now also proves global all-volume invalidation, projection-eviction restart safety, captured include/exclude filtering across hot/cold and rename/move transitions, partial-parent fail-closed behavior, and durable fallback snapshot stability. The UI lifecycle suite proves `UiServer::Stop` cancels and joins a live fallback without publishing a result; seeded indexed-mode acceptance remains green, including retained offline rows.
- Focused build evidence: MSVC/Ninja rebuilt `ffengine_index_pipeline_concurrency_tests`, `ffengine_ui_server_lifecycle_tests`, `ffengine_bench_large_index_scalability`, `ffengine_index_pipeline_tests`, `ffindexstore_read_session_tests`, `ffsearch_tests`, and `ffengine_seeded_indexed_mode_tests` warning-clean. All six direct executables passed after the latest fallback timing and rule-filter changes.
- Final local Debug benchmark evidence: the 1K run completed the hot selective/broad and cold selective/broad stages, sustained-write broad fallback after four restarts, fallback cancellation, contention, and the snapshot capacity probe. The fallback completed with 296 results/1,004 rows streamed; cancellation entered fallback and published zero rows; peak searches/readers were 2/2 and cleanup ended at zero active readers/searches. Detailed values are in `docs/benchmarks/large-index-scalability.md` and the ignored generated report.
- Rule/reconciliation focused evidence: the concurrency suite passes the new stale-excluded-row reconciliation assertion after the hot/cold/scoped/rename/move and partial-parent checks.
- Portability evidence: the initial local Debug CTest reproduced seven failures: the package-installed PowerShell launcher caused five `BAD_COMMAND` child tests and the same launcher path caused the portable AuthentiCode fixture failure. `cmake/FastFilesPowerShell.cmake` now prefers the per-user execution alias when the cached path is under `Program Files\\WindowsApps`; focused children and the portable AuthentiCode test pass, and the final Debug CTest is **78/78 PASS**.
- Concurrency evidence: the initial full-suite barrier timeout did not reproduce in isolation; increasing the condition-variable watchdog from 2s to 10s (explicitly non-semantic) made the final full-suite concurrency test pass. The final direct/CTest focused tests and full Debug suite are green.
- Validation checkpoint: release-test CTest is **78/78 PASS**; Analyze build is warning-clean; ASAN protocol fuzz, IPC framing, and IPC listener lifecycle pass under `VsDevCmd`; `verify.ps1 doctor` passes; strict OpenSpec validation passes **65/65**.
- Final local policy checkpoint: `verify.ps1 build` passed on an isolated rerun after an initial opaque host-contention failure; source-marker and diagnostic-policy gates pass; `ffdoc_drift_tests` passes; `git diff --check` and tracked-artifact review are clean. The first unsigned release-candidate attempt correctly refused the dirty checkout (`RELEASE_CLEAN_TREE_REQUIRED`); rerun it after the logical commits.
- Clean release-candidate checkpoint: unsigned-validation qualification passed against clean baseline `d0dc5f4`, producing a valid portable x64 kit with consistent build identity and `productionReady: false` as required for unsigned artifacts. The clean-tree gate and packaging/provenance checks passed without trust-store or signer changes.
- Final remote/PR audit: after `git fetch --prune origin`, the pushed implementation/handoff baseline `930069b3` is **32 ahead / 0 behind** `origin/main`; `git merge-base --is-ancestor origin/main HEAD` passes. Public PR #6 reinspection shows it is still draft/open at head `9ddf1a3`, with no comments/reviews; no authenticated GitHub close/write was attempted. Full supersession remains documented in `docs/pr6-stabilization-reconciliation.md`.
- Hosted handoff evidence: Actions run `32712997710` for `930069b3f86585c89cb11439aee6342142b4157b` completed **success**. Debug, release-test, Analyze, ASAN protocol/IPC, repository hygiene/contracts, and unsigned release qualification all passed. The final state commit is intentionally not represented by a self-referential SHA; derive the live tip and remote tracking state with Git on resume.
- NEXT EXACT ACTION: run release-test, Analyze, ASAN, verifier, strict OpenSpec, and hygiene gates; then commit logical waves and reconcile/push the release train. Keep tasks checked only with concrete evidence.

## Phase-9 read-plane checkpoint (2026-08-24)

- Live reconciliation completed: fetched/pruned remotes; local Phase-8 branch was `27b521b321b57e3553da1c246d9080bd42e2224c`; `origin/main` advanced to `d8d52abec4ad5e5b49c82fc1e9a63637c6ea5dd1` with an additive planner-handoff commit; the Phase-9 reconciliation merge is `55408b7` and the working tree is intentionally dirty with the campaign implementation, tests, specs, and documentation.
- Public GitHub API confirms PR #6 (`https://github.com/quantdale/FastFiles/pull/6`) is still open, with no reviews/comments returned; `gh` is unauthenticated, so no close/PR mutation was attempted. Hosted Phase-8 CI run `32685840596` completed `success`; current `origin/main` CI run `32687641774` was `in_progress` at audit time.
- Open task ledgers were re-read: only `autonomous-runtime-verification`, `close-independent-validation-gaps`, `phase8-release-readiness-convergence`, `real-user-dogfooding-release-engineering`, and `zero-touch-autonomous-engineering` contain unchecked tasks. Their remaining tasks are external/provider/signing/interactive or handoff prerequisites; no stale Phase-8 code or PR #6 delta was ported.
- The Phase-9 canonical sync is complete: `openspec validate --all --strict --no-interactive` passes **64/64** after reconciling the older concurrent-access and memory-bounded deltas with the new independent-reader, bounded-search, and diagnostics scenarios. The Phase-9 task ledger records 7.3 and 7.5 as evidenced; only final validation/commit/push handoff items remain open.
- The pre-refactor defect was structural: `IndexPipeline::SearchIndexed` held `mutex_` across projection enumeration, cold `Store::ForEachEntryUntil`, hybrid path reconstruction, matching, and sorting; `Store` was not internally synchronized and had no independent read session; `UiServer` had one search worker per client with no global admission bound. The deterministic baseline recorded all three mutation/read callers incomplete until the parked search released.
- Created `openspec/changes/nonblocking-indexed-read-plane/` with proposal, two new capability specs, two modified capability deltas, design decisions D1–D8, and the task ledger. `openspec validate nonblocking-indexed-read-plane --type change --strict --no-interactive` passes.
- Chosen design: monotonic generation + bounded hot candidate chunks + deterministic whole-query restart on generation changes; move-only query-only `StoreReadSession` per cold query; bounded FIFO coordinator (2 active / 8 queued by default); bounded top-N accumulation and no full owning candidate vector.
- The fixed `ffengine_index_pipeline_concurrency_tests` now requires ingestion, aggregate, and snapshot export to complete while the query is parked outside the mutex; it also covers cold reader faults, mid-stream fail-closed behavior, cancellation/resource release, and pipeline-close cancellation. The baseline used a condition-variable barrier, not timing-based sleeps.
- Implementation milestone: `StoreReadSession` owns an independent read-only SQLite connection and explicit WAL snapshot; `SearchAccumulator` performs bounded top-N work outside pipeline state; `IndexPipeline` captures generation/prefixes/readers, copies hot rows in 256-row chunks, streams cold rows out of lock, and restarts up to three times on generation change; `UiServer` bounds actual client workers at two active/eight queued FIFO with queued cancellation and shutdown drain. A coordinator pointer race was closed with a dedicated mutex.
- Focused evidence: `ffindexstore_read_session_tests`, `ffengine_index_pipeline_tests`, `ffengine_ui_server_lifecycle_tests`, `ffengine_seeded_indexed_mode_tests`, and `ffengine_index_pipeline_concurrency_tests` all pass after the implementation. The 10K Phase-9 benchmark probe recorded ingestion/aggregate/snapshot/second-search progress while a cold search was held, sub-millisecond cancellation, two peak readers, and zero readers after cleanup; details are in `docs/benchmarks/large-index-scalability.md` and the temporary machine-readable run.
- Final verification evidence: the exact `pwsh ./verify/verify.ps1 build` rerun passes after the fail-closed aggregate/path/serialization fixes; release-test CTest is **78/78**, ASAN protocol/IPC is **2/2**, doctor/source-marker/diagnostic-policy/doc-drift/strict OpenSpec all pass, and Analyze is warning-clean. The final two 10K reruns are slower wall-clock measurements under substantial unrelated host CPU load; stage counters and the structural probe show bounded state-lock holds and no unbounded reader/memory fan-out, documented without timing thresholds.
- A later direct Debug CTest rerun passed all C++ product/engine/UI/store targets, including `ffengine_index_pipeline_concurrency_tests`, but the host failed the portable temporary Authenticode fixture and refused to start five PowerShell-registered tests with `pwsh.exe [operation not permitted]`. The focused Authenticode retry reproduced only the fixture-generation failure while ordinary fail-closed assertions passed and cleanup left no files; this remains an environment retry item, not a Phase-9 code failure.

### Phase-9 next frontier

1. Run direct Debug configure/build/full CTest and the final diff/artifact gates after the last source/spec edits.
2. Commit the implementation, tests, benchmark, documentation, and OpenSpec sync in logical waves; keep generated verification/scratch output out of Git.
3. Fetch/reconcile, push the stacked branch if credentials permit, inspect hosted CI for the exact final SHA, and write the final handoff checkpoint. — commits `c47ec3a`, `9a02207`, `ba04dc6`, and `7c4c7f1` are pushed; run `32698159780` is the live final-SHA check.

## Phase-8 convergence checkpoint (2026-08-24)

- Revalidated the existing `memory-bounded-indexed-scale` implementation against its task ledger and focused executables; all 25 tasks are now checked with evidence. Strict OpenSpec validation currently passes **61/61** items.
- Added new OpenSpec change `phase8-release-readiness-convergence` for machine-generated status/frontier evidence, provider preflight/lifecycle contracts, release/CI qualification, and the audit ledger. Its planning artifacts validate strictly; external guest/signing/clean-host/interactive tasks remain intentionally open.
- Added `verify/core/ProjectStatus.psm1`, `verify/status/Get-ProjectStatus.ps1`, schema/tests, and `New-ReleaseReadinessReport.ps1`. `progress.md` is now a stable entry point; dynamic counts are generated rather than hand-maintained. Status tests pass and generated status validates against schema.
- Provider contract now requires reason-coded `preflight`; `Invoke-EnvironmentProviderContract` guarantees lifecycle ordering and cleanup. Hyper-V accepts a runtime-supplied source path without committed machine paths, emits stable unavailability codes, generates a plan-only bootstrap description, and never claims guest execution. Fake provider contract tests pass across success, lifecycle failures, cancellation/resume, malformed/unsupported descriptors, dependency absence, and repeated cleanup.
- Debug was reconfigured/built successfully and full CTest is **76/76 PASS**, including `ffproject_status_ps_tests` and `ffprovider_contract_ps_tests`; doctor found the VS/CMake/Ninja/CTest toolchain and confirmed Hyper-V/Sandbox unavailable. Release-test/analyze/ASAN gates remain next.
- CI now has separate hygiene/OpenSpec/status/provider and unsigned release-candidate qualification jobs; no signer material or unsafe interactive artifacts are uploaded.
- The diagnostic UIA build now uses a marked per-session LocalAppData/AppData/fixture sandbox and semantic main-window/fixture-row waits. A direct diagnostic `core` run passed all selected scenarios with only genuine privileged-service subchecks skipped; the former selection failure was a stale terminal-column provider after navigation and is fixed by live-column reacquisition.
- `verify/core/Schema.psm1` now accepts ISO timestamp values materialized as `DateTime` by `ConvertFrom-Json`; the frontier schema round-trip test caught and covers this harness defect. First convergence commit: `cf54366` (`phase8: add status and provider readiness contracts`).
- Clean-tree `Test-ReleaseCandidate.ps1` passed: both unsigned candidates emitted checksums/provenance/release notes/dependency inventory, repeated archives had identical SHA-256, and the production candidate rejected missing detached-manifest signing. The CI/release tasks 3.1–3.4 are now checked with this evidence; the task-ledger update is the next commit.
- Composed release readiness at clean `628a560` completed in 460.3 seconds: 35 PASS, 8 SKIPPED (expected unelevated/optional gates), 1 REQUIRED-BUT-UNAVAILABLE (disposable guest matrix), and 1 FAIL for the existing unsigned/stale production artifact report on a contaminated non-elevated host. It did not mutate the host or trust stores.
- Audit ledger `codebase-audit/sessions/2026-08-24-phase8-convergence` records FF-P8-001..006. The report sanitizer was then hardened for slash-normalized absolute paths in `84b909e`; status/frontier tests pass again.
- Final validation repeated Debug CTest **76/76 PASS**, release-test CTest **76/76 PASS**, Analyze build, ASAN protocol/IPC **2/2 PASS**, source/diagnostic policy, doctor, strict OpenSpec **61/61**, and diagnostic UIA `core` **PASS**. CTest JUnit evidence now drives the status report; stale `LastTestsFailed.log` no longer creates false FAILs. FF-P8-007 is recorded and fixed in `f5e7812`.
- Final handoff checks: exact `pwsh ./verify/verify.ps1 build` **PASS**; focused `ffdoc_drift_tests`, `ffproject_status_ps_tests`, and `ffprovider_contract_ps_tests` **3/3 PASS**; strict OpenSpec re-run **61/61 PASS**. Hosted CI run `32685106949` for baseline `32bc59b` was publicly observable as `in_progress` at the checkpoint; no hosted PASS is claimed yet.

## Phase-8 checkpoint (memory-bounded-indexed-scale)

- Core defect reproduced and fixed: `SearchIndexed` called `ReloadAllEvictedVolumes()` under the pipeline mutex (ceiling violation, reload→evict thrash, ignored-failure partials). Replaced by a per-volume hot/cold partition: cold volumes stream durably (`Store::ForEachEntryUntil`), trigram gated to all-hot sets, hybrid folder reconstruction via new `Store::FindEntry`, fail-closed `Unavailable` on broken cold streams, cancellation inside streams.
- Memory policy observable: `Projection::MemoryDiagnostics` + `IndexPipeline::{SetProjectionMemoryBudget,GetProjectionMemoryDiagnostics,IsVolumeEvicted,UnloadVolumeFromProjection}`; `Projection::EvictVolume` deterministic unload.
- ffsearch: total-order tie-breaks (name→folder→volume→id) + bounded top-N heap truncation — capped results no longer depend on enumeration order (was pre-sort scan-order truncation).
- Snapshot publication bounded: `ExportDirectorySnapshotBounded(kInitialIndexedExportDirectories=4096)` at startup/scan-complete; `UiServer` retained indexed working set cap 16384 w/ observation-epoch eviction (`SetIndexedWorkingSetCap`, `GetSnapshotWorkingSetStats`); degraded entries immune; per-volume replace cleanup intact; precedence rules from deterministic-missing-path-state untouched.
- New tests: forced-eviction equivalence suite (13 query classes × reference-vs-evicted), no-thrash loop, cold mutations/restart/reconciliation, cold aggregates, cold-stream cancellation, UiServer working-set behavior, `Store::FindEntry`, projection diagnostics, and `ffengine_seeded_indexed_mode_tests` (seeded acceptance: seed→restart→export→publish/parse→search→aggregates→offline→mutations→reconcile→terminal state).
- Gates this session: Debug CTest **74/74**, release-test CTest **74/74**, analyze build warning-clean, ASAN protocol+IPC fuzz **2/2**, strict OpenSpec **60/60** incl. new change, doc-drift PASS after CRLF normalization of every edited file (recurring hazard: the edit tool emits LF).
- Bench evidence: `docs/benchmarks/phase8-large-index-scalability.{md,json}` (100K/500K/1M Release, hot+warm+cold stages; result counts identical hot vs cold at every class) plus 2M run JSON; capacity probe ≈2.2M rows at the 256 MiB cap.
- External/unavailable: GitHub push/PR + PR #6 closure (network unreachable), production signer/clean-host MFT-USN evidence (unchanged, evidence-gated), hosted CI observation for pushed SHAs.

## Current overnight campaign checkpoint (2026-08-23)

- Repository reconciliation: fetched/pruned refs, clean `main` at `e8b1d9b076cb2e194ca007303689e71dfe44f671`, created `overnight/release-closure-20260823` from exact `origin/main`; PR #6 head is `9ddf1a3189be32db11c97e7c79bb7404caac1d8f`, merge base `90fa560a99217cabcf456b1198c83351c46513ed`.
- PR #6 unique changes are seven files: a documentation audit, a palette refinement plus contrast tests, display-only 8.3 navigation handling, and themed Storage button controls. Current main already has the newer `DisplayPath` normalization and `StorageRadioState`/radio implementation; those PR variants are provisionally superseded. The palette and stronger contrast assertions remain candidates for deliberate porting after exact consumer/background review.
- Existing release readiness is artifact-only: `verify/verify.ps1 release-readiness` delegates to `verify/release/ReleaseReadiness.ps1`, which reports build/tests/OpenSpec as `NOT_RUN`. The main implementation frontier is one structured composition layer that invokes/reports existing gates without weakening production signing or converting external blockers to PASS.
- Current open task inventory from `tasks.md`: `autonomous-runtime-verification` 2 provider tasks; `close-independent-validation-gaps` 6 external/visual tasks; `real-user-dogfooding-release-engineering` task 5.4; `zero-touch-autonomous-engineering` 3 provider tasks. Test-PKI, production signer, clean-host, guest, DPI, and real privileged MFT/USN claims remain evidence-gated.
- Hosted CI truth is not yet available from local `gh` because the GitHub CLI is unauthenticated; inspect the workflow and query the pushed SHA through an authenticated mechanism if available, otherwise record the exact external limitation.

## Overnight implementation wave (2026-08-23)

- PR #6 disposition is now behaviorally established: the display-only navigation and themed Storage button deltas are superseded by current-main `DisplayPath` and `StorageRadioState` implementations; the audit document is provenance-only; the unique palette/contrast work was ported deliberately into the current `UITheme` token set. `textOnAccent` is deep navy in dark mode for readable selected text; light/dark primary, secondary, tertiary, disabled, glyph, focus, selected, and status pairings are covered by `ffui_style_tests` with alpha compositing for the informational chip.
- `verify/release/ReleaseReadinessCompose.psm1` plus `ReleaseReadiness.ps1 -Mode compose` and `verify.ps1` forwarding form one composition path over the existing verifier/release tooling. It records exact SHA/branch/dirty state, version, x64/configuration/toolchain/host fingerprint, per-gate status/reason/duration/artifacts/diagnostics, child run paths, ASAN protocol+IPC tests, strict OpenSpec, source/diagnostic/doc-drift guards, candidate packaging, and production artifact readiness. `-IncludePrivileged` and `-IncludeInteractive` are explicit; test-PKI is never implicit. Two harness issues found during first composition runs were fixed: New-Item parameter misuse, inline PowerShell if expressions, unsuppressed List.Add output, and runtime architecture metadata using a null API.
- Validation evidence in this wave: focused `ffui_style_tests` PASS; focused `ffdoc_drift_tests` PASS after CRLF normalization; standalone `verify.ps1 build -Change release-readiness` PASS; ASAN protocol+IPC 2/2 PASS; strict OpenSpec 59/59 PASS; source-marker and diagnostic-policy PASS. A first independent Debug CTest reached 72/73 product tests PASS and only failed the mixed-line-ending guard before normalization; the subsequent release-test CTest was 73/73 PASS. The composed run reached ASAN and all child work but needs one final rerun after the last metadata fix to produce its JSON verdict.

## Test-PKI and privileged-trust checkpoint (2026-08-23)

- `verify/capabilities/test-code-signing/TestCodeSigning.psm1` was audited and hardened transactionally: each run uses a unique certificate subject, backs up all four Debug product binaries and restores bytes/attributes/timestamps, removes only its exact CurrentUser\My certificate, and emits cleanup evidence without returning PFX/password paths. The original PFX export was found to stall on this host; the capability now selects the unique certificate from CurrentUser\My with bounded signtool execution and never mutates CurrentUser\Root during ordinary runs.
- Direct structured test-PKI result: certificate generation and all four binary signatures PASS; signer thumbprints were present; positive WinVerifyTrust/pinned acceptance was explicitly `SKIPPED(current-user-root-trust-unavailable)` because WinVerifyTrust returned false and PowerShell reported `UnknownError` for the untrusted self-signed chain. Binary hashes matched backups after cleanup. This is non-production evidence and does not satisfy production signer/runtime tasks.
- The repository's opt-in `verify/run_privileged_trust_tests.ps1` configured and built the dedicated suite successfully, but all three trusted-signer/expiry/timestamp cases failed on this host and its residue check found two pre-existing marker-named roots in CurrentUser\Root. Exact removal attempts via X509Store and `certutil` blocked/hung under the host's ProtectedRoots policy; no unrelated roots were touched. The two remaining thumbprints are recorded in the session evidence, and this is an environment-only blocker requiring a host policy/state change.
- The composed readiness run at clean implementation SHA `fb8eb7352fccbd892dbc1459a59ea43a969a10c5` completed in 495.7 seconds and produced a valid report: 45 gates = 35 PASS, 8 SKIPPED, 1 REQUIRED-BUT-UNAVAILABLE, 1 FAIL. The only FAIL was the production artifact report: stale `build/release` metadata identity and unsigned binaries, plus a pre-existing installed service/task state; the host was not mutated. Candidate packaging/provenance passed. Final serializer scope bugs were fixed in commits `0030ae9` and `4d87b6e`.
- Hosted handoff: push-triggered CI run `32648989813` was attached to final code/docs SHA `9b5285a2e5c9eca6f197d742195a39e5d01bc992`; API observation recorded `asan-protocol-fuzz`, `debug`, and `release-test` as successful, and the public branch workflow badge subsequently reported `CI - passing`. The unauthenticated API quota prevented a final per-job Analyze fetch, but no failure was reported and the overall workflow status is passing. `git fetch origin --prune` reconciled `origin/main` at `e8b1d9b076cb2e194ca007303689e71dfe44f671` and the remote campaign branch at the pushed SHA. PR #6 remains open because the local GitHub CLI is unauthenticated; its unique deltas are fully dispositioned in this checkpoint.

## Current 2026-08-23 Phase-7 certification checkpoint

- Reconciled at campaign start: branch `main`, local `HEAD == origin/main == 90fa560a99217cabcf456b1198c83351c46513ed`, clean before implementation. `gh run list` was unavailable because the local GitHub CLI session is unauthenticated; re-query for the final pushed SHA.
- Deterministic visual harness is implemented under `verify/visual/` and `verify/capabilities/windows-ui-visual-validation/`. It backs up/restores product state, uses a disposable mapped fixture drive, requires semantic UIA postconditions before capture, captures via DWM extended bounds + `CopyFromScreen`, rejects unexpected duplicate hashes, and writes manifest/Markdown evidence. The refreshed final run produced 53 rows: 26 PASS images, 2 SKIPPED, 25 BLOCKED_ENVIRONMENT, 0 FAIL; 3 Home images include recorded semantic privacy redactions; no personal path text was present in manifest/summary or committed visual evidence. The dark Storage Analysis row is included and passed.
- UIA flake audit: treemap root cause (missing `return` after a failed sub-navigation assertion) fixed; drag root cause (stale pre-toggle ColumnView provider and unchecked foreground/pane activation) fixed; selection root causes (unchecked foreground, stale/unsafe synthetic click sequence, `.txt` shell association contamination, and incorrect Pane.HasKeyboardFocus expectation when a row owns focus) fixed in the harness. Fail-fast `FailFast` wrappers retain per-attempt UIA tree/screenshot/log/foreground diagnostics and never retry by default. Treemap isolated/group runs pass repeatedly; drag group is PASS with documented Explorer-source and host-foreground skips; selection is PASS-or-explicit `SKIPPED/interactive-foreground-unavailable` when `browser-blackhole`/`clone-MC_v3` owns the desktop foreground, with no downstream assertion attempted.
- Local UI debt implemented: tokenized custom paint for Storage Analysis native radio buttons while preserving native radio semantics, keyboard behavior, UIA RadioButton/SelectionItem providers, High Contrast fallbacks, and checked/focus/disabled states; display-only 8.3/long-path normalization via `ffui::NormalizeDisplayPath` across navigation chrome, tabs, Home, UIA, Search, sidebar, and details while canonical operational paths remain unchanged. Focused `ffstorage_radio_state_tests` and `ffdisplay_path_tests` passed on the diagnostic build; Debug and diagnostic FastFiles builds passed after the changes.
- Repository gates after the evidence refresh: Debug CTest `73/73 PASS`; release-test CTest `73/73 PASS`; Analyze build completed warning-clean; ASAN `ffprotocol_fuzz_tests` `1/1 PASS`; source markers and diagnostic policy passed; visual-capture contract passed; strict OpenSpec validation passed `59/59`. The repository has no standalone `verify/check_doc_drift.ps1`; the `ffdoc_drift_tests` CTest passed in both full configurations.
- Current OpenSpec checkbox truth (live `tasks.md` audit): complete: `allocated-size-storage-accuracy` 36/36, `indexed-search-allocated-size` 74/74, `semantic-treemap-uia` 40/40, plus all previously closed changes; genuinely open: `autonomous-runtime-verification` 2 external/provider tasks, `close-independent-validation-gaps` 6 external/visual-validation tasks, `real-user-dogfooding-release-engineering` 1 final push/CI task, and `zero-touch-autonomous-engineering` 3 external/provider tasks. Do not claim production signer, signed privileged runtime, clean-host MFT/USN, or usable Windows guest/provider evidence.
- Campaign-owned untracked UIA repeat artifacts include raw failure screenshots that may contain the interactive desktop; they must remain local/ignored execution evidence or be removed before final clean handoff. Only sanitized visual-catalog evidence may be committed.

## What the interrupted 2026-08-22 session actually left behind

- `d1680df` committed the DropFeedbackLayout seam WITH build+ctest verification (70/70 at that time) — the old "build-pending" caveat was already resolved before this campaign.
- `c789f10` → `d43201a` → `5c905b9`: search button UIA semantics (real Button control type, Invoke, keyboard traversal, ffsearch_tab_order_tests) and the address-bar Ctrl+L retry hardening. These are CURRENT functionality; do not redo them.
- `91390e7` added 22 PNG captures under `codebase-audit/sessions/2026-08-22-ui-v3-final/visual/` and nothing else. Audit findings about that evidence set:
  - Three duplicate-image groups (`02=03=04`, `12=13`, `15=16` — byte-identical blobs): the state transitions silently failed and the script captured anyway. Those files are INVALID evidence for their labeled states.
  - Numbering gaps (07 conflict dialog, 19/20 drop feedback planned but skipped); `18-wide` resize failed; several labels mismatched actual content (01 "home" showed restored workspace; personal paths leaked into committed artifacts contrary to the screenshot policy).
  - Root cause of the silent duplicates: the capture harness had NO post-condition assertions and swallowed failures with try/catch.

## Defects found by this campaign's visual audit — ALL FIXED AND LIVE-VERIFIED

Evidence method note: the earlier "white combos/buttons" readings were small-screenshot misreads (zoomed crops + pixel sampling corrected the record). Pixel sampling (GetPixel) plus forced-redraw probes were used as ground truth throughout.

1. **V3-01 (P1) overlay bleed-through**: Search/Storage surfaces rendered ColumnView beneath inset native child controls; every gutter showed stale browse pixels (left icon strips, heading ghosts, right letter slivers, bottom yellow strips). Fix: `WindowShell::Render` clears to theme background instead of rendering columns when Search/Storage is visible (treemap branch unchanged).
2. **V3-02 (P1) system-white results header in dark**: design intent was to hide the ListView header, but a bare `SW_HIDE` raced the list's first layout and re-showed. Fix: `LVS_NOCOLUMNHEADER` at creation (column widths still drive scroll extent). An interim themed NM_CUSTOMDRAW implementation proved the mechanism live and was removed as dead code once the style fix landed.
3. **V3-03 (P1) stale viewport after navigation**: `NavigateToPath/Hierarchy/ComputerRoot` rebuilt the column chain but never touched horizontal scroll; the leaf stayed off-screen right (captures showed C:\ children while breadcrumb claimed a deep path). Fix: `EnsureColumnVisible(FocusedColumnIndex(), NavigationViewportWidth())` after every navigation mutation site (NavigateToLocation, NavigateColumnViewTo, NavigateWorkspace selectName branch, RestoreActiveTabViewState fallbacks).
4. **V3-04 (P1) Home recents text collision**: home primary text drew raw `DrawTextW` with a NO_WRAP format lacking trimming; long recent-folder paths painted across grid columns. Fix: `DrawHomeText` routes through the existing bounded character-ellipsis `DrawRowLabel`.
5. **V3-06 (P2) storage toolbar label clipping**: compact packing used a 44-DIP minimum button width; native radio labels have no ellipsis ("Largest Fol…"). Fix: `kLabelFitButtonWidth=118` in the headless seam (two-row wrap when needed); status static gets `SS_ENDELLIPSIS | SS_LEFTNOWORDWRAP`; test updated to assert the wrap + label-fit invariant.
6. **V3-07 (P2) treemap blank canvas**: empty layout returned silently. Fix: deliberate state surface ("Calculating folder sizes…" during scope load, flipped to "No sized data for this scope." on completed-empty aggregate) via `TreemapView::SetEmptyStateText`.
7. **Storage dark mode (same defect class, found during V3-06 verification)**: overview/drill-down ListViews kept system-white backgrounds because the lazily-initialized panel never received the startup theme. Fix: `SetDarkTheme(gUiDarkTheme)` at Initialize end; ListView colors themed; headers themed via NM_CUSTOMDRAW (kept here because these data tables need headers, unlike search).
8. **Query-field white flash**: keystroke-driven edits repainted natively and left stale light frames over the custom paint until an unrelated repaint. Fix: SearchEditProc invalidates after WM_KEYDOWN/WM_CHAR. Verified by forced-redraw probe (before: white; after RDW_UPDATENOW: correct dark).

Verification evidence (this host, diagnostic build): zoomed crops + GetPixel samples show dark field/header/body where white stood before; deep navigate reveals the leaf column; Home recents trim without overlap; storage toolbar wraps with full labels; idle probe: UI 0 ms CPU over 20 s (no animation timer while idle), handles/threads/WS flat (1171/30/61.1 MB), engine ~0.07 %.

## Prior 2026-08-22 gates (historical checkpoint)

- Debug build warning-clean; Debug ctest **71/71 PASS**; release-test build + ctest **71/71 PASS**; analyze `/analyze` build clean.
- OpenSpec strict validation **59/59 PASS**; source-marker PASS (Failures: 0); diagnostic policy PASS; doc-drift/line-endings PASS (after CRLF normalization of edited files); `git diff --check` PASS.
- Full `windows-ui-automation-validation` capability via `verify.ps1 run -Capability windows-ui-automation-validation`: **status=PASS** (run tree ad-hoc `20260822-211341`). Honesty note: an earlier same-session invocation returned `scenario-failed`; the immediate rerun passed fully. Consistent with the registry's documented geometry-flaky scenarios (treemap-readability-click-precision, selection-model-interactions, drag-between-panes are Flaky=true); NOT counted as clean-without-caveat — flagged for a repro attempt in the next slice.

## Prior capture-harness requirements (catalog now rebuilt in current checkpoint)

The mandatory catalog (docs/ui-improvement/05 §1) is still only partially covered. Rebuild rules learned here:
- Reset `%LOCALAPPDATA%\FastFiles\workspace-state.json` (back it up first) so captures start deterministic; never rely on restored personal tabs.
- Use sanitized fixtures under `%TEMP%\ffviz-*` (long names for trim/reflow coverage); never navigate personal folders in captured states.
- Screen-region CopyFromScreen of DWMWA_EXTENDED_FRAME_BOUNDS is the capture method (PrintWindow matched it everywhere tested, but region capture is ground truth by construction).
- ASSERT post-conditions via UIA (element exists/state changed) BEFORE capturing; fail loudly instead of capturing a duplicate.
- Dedupe guard: compare blob hashes across the batch; abort on unexpected duplicates.
- Known-good chords: Ctrl+F opens search (NOT Ctrl+Shift+F); palette flow Ctrl+Shift+P → AutomationId 6301 → query → Enter works ('Home'/'Analyze' commands verified); typing "Home" into Ctrl+L produces the honest red validation banner instead of navigation.
- Host boundaries (unchanged): single 1920x1080 @96 DPI monitor means live monitor-to-monitor PerMonitorV2 transition and live 125/150/200 percent scale matrix remain BLOCKED_EXTERNAL (headless coverage: TestFullClientCoverageInvariant at 1.0/1.25/1.5/2.0 x details x panes); High Contrast toggle not exercised this session (invasive host setting; code paths HC-gated and unit-covered elsewhere).
- Animations-off live capture NOT EXERCISED: SystemParametersInfo(SPI_SETCLIENTAREAANIMATION=0x1043) reports success but readback stays enabled from this session context - do not retry blindly; the product-side gate (SPI_GETCLIENTAREAANIMATION via UiAnimation, snap-instant when disabled) is unit-covered instead.
- HOST-SETTING SAFETY LESSON (2026-08-23): an SPI probe initially used WRONG constants (0x4A/0x4B are FONT SMOOTHING, not client-area animation which is 0x1042/0x1043), turning ClearType off host-wide. Recovery: registry FontSmoothing=2 plus SystemParametersInfoW(SPI_SETFONTSMOOTHING with uiParam=1 and pvParam=NULL, f=3) - the ref-BOOL variant does not stick in this session context. Final verified state: fontSmoothing=1, clientAreaAnimation=1, registry=2. Always read back after any host-setting probe and restore immediately.

## Prior next recommended slices (historical checkpoint)

1. Rebuild the full visual-state catalog per the rules above (largest open Phase-7 item; mostly mechanical now that the harness contract is written down).
2. Reproduce-and-bound the intermittent UIA scenario failure noted above (flaky-test policy: root cause or documented bound, never retry-as-resolution).
3. Native radio buttons/storage toolbar remain system-styled in dark mode (P3 debt class shared with other Win32 chrome); owner-draw radios would close the last system-light controls in the storage surface.
4. Breadcrumb/status display still shows 8.3 short names (MICHAEL~1) when navigated via short-path environments — display-side normalization candidate (P3).
5. OpenSpec changes with open tasks (per tasks.md checkbox counts, verify before implementing): close-independent-validation-gaps, allocated-size-storage-accuracy, autonomous-runtime-verification (external/provider), indexed-search-allocated-size, semantic-treemap-uia, zero-touch-autonomous-engineering.

Historical records below are retained unchanged for provenance.

---

## Shell-outage forensics + TreemapView decision (2026-08-22, ox-alpha) [SUPERSEDED STATUS — outage resolved; content retained]

- Outage forensics (all read-only, no shell available): server log shows bash permission evaluations occurred normally on 2026-08-20 but ZERO on 08-21/08-22 while sessions kept streaming -> failure sits between parse and permission ask inside the shell tool execute path; tool factory still logs `shell tool using shell` fine. `shell.ts` is byte-identical between tags v1.18.19 and v1.18.21 -> provisioning/deps regression, not the tool. Node `path.resolve` semantics confirm the exact message only when an argument is undefined. Failed workarounds (documented so nobody retries): disabling plugins mid-session (no effect), DEBUG logLevel (needs restart), custom-tool shim hot-load in BOTH `~/.config/opencode/tool/` and project `.opencode/tool/` (registry is boot-time; fresh subagent sessions do not re-scan).
- Product decision RECORDED (closes UI-v3 audit item): treemap exposes exactly base tile + hover lift; NO selected/pressed/keyboard-focus state by design - activation always navigates away (drill-in-place or Column View with file selected per spec), hover already carries full canonical metadata, keyboard traversal would need its own spec delta. Full rationale appended as `semantic-treemap-uia/design.md` section 21. No code change required.

## UI-v3 mass implementation campaign (2026-08-22) - historical checkpoint

- Branch consolidation merge-base `1d4fb45`; foundation `b984be0`; waves A/…/settings `660a8d6`, `01136ef`, `2e2db34`, `da04799`, `2b40be6`, `6f31e2c`, `19bd47a`, `a8b135f`. All waves built clean under /W4 /WX before commit.
- Deferred-by-design items were later closed: DrawDropTargetFeedback headless seam + test (`d1680df`), OleDragDrop custom drop-target visuals (`19bd47a`), TreemapView state question (design.md section 21).

## Real-user dogfooding and release-engineering campaign (2026-08-21) — historical

- Fixture/acceptance wave, release candidate wave, UIA wave, measurement wave all complete as recorded below; RLS-001 and UX-001 resolved; final gates Debug/release-test 69/69 each at that time, /analyze clean, ASan fuzz PASS, strict OpenSpec PASS, final all-scenario UIA acceptance PASS.

## Final production-readiness closure pass (2026-08-21) — historical

- Bottom-edge strip root-caused (DWM invisible borders + WM_DPICHANGED client sizing), fixed, and covered by TestFullClientCoverageInvariant; targeted/full UIA certification runs passed; external blockers intentionally unchanged (production signing pins, signed install, elevated clean-host MFT/USN matrix, guest/VM provisioning).
