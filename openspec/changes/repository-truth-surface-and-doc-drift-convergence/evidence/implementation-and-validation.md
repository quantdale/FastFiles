# Implementation and validation record

Campaign: `repository-truth-surface-and-doc-drift-convergence`
Baseline: `5758bbfde24da5a0076373abcbc3e3d585ad2895` (planner tip, clean,
equal to `origin/main` at start).

## Implementation

- `tests/drift/DocTruth.psm1` — semantic truth contract engine
  (authority extractors for verifier verbs/exit codes from `verify.ps1`,
  presets from `CMakePresets.json`, first-party `FastFiles*` executables and
  `ff*` static libs from `src/**/CMakeLists.txt`, schema-v2 lifecycle states,
  snapshot publisher source seam) plus fail-closed fence parsing, sentence-
  segmented anti-staleness patterns (`STALE_CURRENT_CLAIM`,
  authoritative-current vs open-tasks modes), removed-mechanic bans, and a
  required stable `openspec/lifecycle.json` pointer per current-facing
  surface. Deterministic sorted findings; repository-relative diagnostics.
- `tests/drift/check_doc_drift.ps1` — composes the engine into the existing
  single fast gate (banned phrases + line endings retained).
- `tests/drift/test_doc_truth_fixtures.ps1` — 19-case adversarial matrix
  against hermetic temp trees (see negative-fixture-matrix.md).
- `tests/drift/CMakeLists.txt` — registers `ffdoc_truth_fixture_tests`
  (pwsh-gated, TIMEOUT 300) beside `ffdoc_drift_tests`.
- Guidance reconciliation: README (full fenced verb enumeration, complete
  preset list fence, test-preset fence, product-executables/shared-libs
  fences, lifecycle-pointer wording replacing stale "current change"
  claims), AGENTS (fenced verbs/exit-codes/presets/test-presets, rewritten
  snapshot paragraph, manifest-pointer open-work formulation, removed
  self-contradictory test-preset sentence), CLAUDE (manifest authority
  replaces stale "current open work"), CODE_INDEX (SnapshotPublisher +
  data-flow step 2 rewritten to committed-generation model, test-dir list
  corrected to actual 17 dirs, lifecycle pointer), progress.md (completed
  retitles/past tense).
- Comment accuracy: `UiProtocol.h` section-name example now includes
  instance+generation composition; `IndexPipeline.h` comment now describes
  committed-generation publication.

## Validation results (observed)

| Gate | Result |
| --- | --- |
| PowerShell parser checks (3 drift scripts) | PASS |
| `tests/drift/test_doc_truth_fixtures.ps1` direct | PASS 19/19 cases |
| `ffdoc_drift_tests` direct | PASS |
| Pre-reconciliation gate behavior | fired exactly F1–F3/F7–F10 classes, zero false positives (tuned: hyphen-compound trigger guard, backtick-required slug refs, scoped case-insensitivity, parenthetical/trailing-period exit-code parsing) |
| Strict OpenSpec `--all --strict` | 72 passed / 0 failed |
| Project status generation (real tree, `-SkipCtest`) | lifecycle PASS; 24 raw unchecked / 0 active-local preserved; HEAD `5758bbf` |
| `verify.ps1 doctor` | PASS (VS 2022 BuildTools toolchain, CMake/Ninja/CTest, SDK; truthful optional skips) |
| `verify.ps1 build` (official gate) | PASS — 9 passed / 0 failed / 0 skipped across debug/release-test + analyze |
| `ctest -R ffdoc` (build/debug) | 2/2 Passed (`ffdoc_drift_tests`, `ffdoc_truth_fixture_tests`) |
| `git diff --check` | PASS |

ASAN protocol/IPC slice: PASS — `ffprotocol_fuzz_tests`,
`ffipc_framing_tests`, `ffipc_listener_lifecycle_tests` 3/3 under
VsDevCmd x64 against the existing `build/asan` tree (97.07s), run despite no
shared build/policy wiring change for completeness.

## CI propagation

`.github/workflows/ci.yml` matrix jobs run `ctest --preset debug` and
`ctest --preset release-test`, which include both drift tests automatically;
no duplicate execution added.

Hosted outcomes: run `32934249659` for `80b24ff` (hygiene + asan +
analyze + release-candidate succeeded; debug/release-test failed on
`ffdoc_drift_tests`) exposed that wave 1 had committed without the
progress.md reconciliation — the guard caught real residual drift on the
committed tree that the dirty-worktree local run could not see. Fixed by
`72db280`; fresh-clone `ctest -R ffdoc` verified green before push; re-hosted
run `32935961728` completed **success** for exact SHA
`72db280e9b41723a571c0f1367aa4ba8bb0be15b`.

## Consumer/transitive audit

- All other verb enumerations live in historical/evidence surfaces
  (`FASTFILES_*.md` root reports, `codebase-audit/sessions/**`,
  completed `openspec/**` designs/tasks, `.agent/**`) — out of scope by the
  historical boundary; recorded in truth-surface-inventory.md.
- Remaining `double-buffered`/`activeSlot` mentions are exclusively in those
  historical surfaces; the only production-source mention (IndexPipeline.h)
  was corrected.
- No product/runtime/security behavior changed; no lifecycle/status/release
  projection semantics touched (status generation re-verified PASS).
