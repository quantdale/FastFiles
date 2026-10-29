# Task 1.3 — local-only and simulation input reconciliation (2026-10-29)

Method: for each unpublished local branch and the old integration simulation,
every `src/`/`tests/`/`cmake/`/`verify/` line the branch added relative to
`origin/main` was checked for verbatim presence in the integrated candidate
tree (`integration/fastfiles-final-product-20261029`), and every residual gap
was inspected and dispositioned by content, not by ancestry. Containment is a
screening signal, not the verdict; the verdicts below are content decisions.

## Accepted into the candidate (unique content, not otherwise present)

| Input | Artifact | Why accepted |
| --- | --- | --- |
| `test/storage-ranking-residency-20261027` (dec145d) | `tests/engine/test_storage_ranking_residency.cpp` + `ffengine_storage_ranking_residency_tests` registration | Journey-E invariant coverage: scoped file ranking across actual hot and cold read planes. Compiled against candidate APIs clean (MSVC /W4 /WX), passed 1/1. Its sibling `test_storage_pressure.cpp` was already present and registered. |
| `test/search-reopen-scope-20261028` (0ac6a3d) | `verify/e2e/Test-SearchReopenScope.ps1`, `verify/e2e/Test-SearchSnapshotRefresh.ps1` | Bounded diagnosis probes for task 8.2 (reopen-scope repro + in-panel snapshot-refresh probe). Harness tools only; no product code. |

## Superseded — features present in evolved form via the accepted PR chains

| Input | Containment | Residual gap disposition |
| --- | --- | --- |
| `fix/search-result-home-handoff-20261025` (8a4f588) | 98.6% | 5 scattered line-deltas; features (Home-aware search reveal, visible command targets, UIA foreground acquisition) present in candidate via PR #18/#19 chains. |
| `feat/storage-semantic-list-20261025` (090f7e2) | 98.8% | Missing lines are inline message-pump drains later generalized into the candidate's shared `DrainOwnedMessages` helper; semantic rows/headers present via PR #26 chain. |
| `fix/visible-uia-workspace-20261025` (5456f1e) | 99.0% | Same helper-evolution pattern; owner-thread invoke and hidden-fragment retirement present via PR #18/#19 chains. |
| `fix/treemap-partial-intelligence-20261027` (c3b26ca) | 98.9% | Partial-Treemap truthfulness and ranking status present via PR #26/#29 chain (status strings identical modulo mojibake `…` repair in the candidate). |
| `fix/compact-storage-navigation-20261027` (180ea28) | 98.1% | Compact Storage selector (4.12) present via PR #26/#29 chain; unique gap = the imported residency test (above) and the superseded P1-2 guard (below). |
| `test/mixed-residency-oracle-20261026` (5173630) | 99.8% | Only substantive gap = 15 `Projection.cpp` lines implementing the **flat 512 bytes/row empty-projection prior** — the exact guard disproven and replaced by the measured-growth guard in PR #14's post-completion audit (`72b4f86`). Correctly excluded (the candidate carries the fix and `TestFittingVolumeReloadIsNotRejectedByEmptyProjectionPrior`). |
| `test/storage-ranking-residency-20261027`, `test/native-uia-client-probe-20261027` | 97.9% / 98.0% | Same gaps as above; harness variants superseded by the accepted PR #23/#28/#31/#35 chain versions of `Test-FileConflictActions.ps1` etc. |
| `test/original-vision-integration-20261025` (e400d70, old simulation) | 98.2% | Composite of the above; its only non-contained content is the imported residency test, the superseded P1-2 flat prior, and older harness revisions superseded by later accepted chains. Treated as evidence input only (design D2), never wholesale imported. |
| `test/foreground-retry-regression-20261028` | 100% (ancestor) | Already an ancestor of PR #35's head. |
| `feat/recursive-storage-ranking-20261027` | 100% (ancestor) | Already an ancestor of PR #29's head. |
| `archive/large-index-local-draft-20261008` | n/a | Archived superseded parallel draft; excluded by its own record (not for merge). |

## Verification of the accepted imports

- `ffengine_storage_ranking_residency_tests`: builds clean under MSVC
  `/W4 /WX /permissive-` against candidate `IndexPipeline` APIs; CTest 1/1 PASS.
- The two probe scripts are PowerShell harness tools with no compile surface;
  they execute only under their own explicit invocations (journey B4 / task 8.2
  bounded diagnosis), never as default CTest entries.

## Claim boundary

This reconciliation establishes `NO_ACCEPTED_FIX_LOST` at the content level for
the listed inputs at the time of intake. Historical raw evidence files were not
rewritten. The one intentionally excluded product block is the disproven
flat-prior guard, superseded by the audited measured-growth guard — recorded
here rather than silently dropped.
