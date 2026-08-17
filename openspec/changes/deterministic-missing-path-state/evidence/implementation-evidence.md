# Evidence — deterministic-missing-path-state

## Root cause analysis (pre-fix)

Three defect chains were identified through code analysis:

### Defect A: Unpublished request failure (D1/D6)
`HandleRequestDirectory` (UiServer.cpp:611-631) short-circuits on `NotFound`/`AccessDenied`/`IoError` by sending a `DirectoryError` frame and returning WITHOUT calling `RepublishAndBroadcastGeneration()`. The snapshot retains the pre-delete `Success`. Any subsequent unrelated generation refresh re-reads that stale `Success`, resurrecting the deleted path.

### Defect B: Stale indexed merge resurrection (D2/D3)
`MergeIndexDirectories` (UiServer.cpp:441-451) unconditionally overwrites `directories_[normalizedPath]` with indexed data. Indexed exports always carry `status = Success`. A stale indexed success for a path that a degraded observation already classified `NotFound` silently overwrites the failure.

### Defect C: Silent watcher terminal error (D4)
`DirectoryWatcher::processCompletion` (DirectoryWatcher.cpp:226-229) logs `FFDIAG_WATCH_FAILED` and terminates the watch without invoking the change callback. When a directory is deleted/disconnected without delivering a prior change notification, the UI never learns the path is gone.

### Defect D: LKG overrides terminal failure (D6)
`SnapshotFreshness::Fail` (SnapshotFreshness.h:67-74) produces `StaleWithError` when a last-known-good generation exists, even for `NotFound`. ColumnView renders stale-retained rows with "Showing previous results..." instead of clearing rows and showing "This folder is no longer available."

## Pre-fix live evidence (historical citation)

Prior live evidence from `stabilize-live-uia-e2e` (evidence/reproduction-and-fix-validation.md, 2026-08-16):
- `in-column-error-state-late` recorded `SKIPPED(error-state-trigger-not-deterministic)` because the engine published an empty-OK directory for an externally deleted folder; the stale-with-error wording ("Showing previous results - refresh failed.") appeared only on a refresh race (observed PASS ~35% of runs).
- The early engine-less phase was `engine-connected-host` (SKIPPED) on the diagnostic build.
- Full capability: 20 PASS / 6 SKIP / 0 FAIL (2026-08-16 baseline).

## Post-fix ordering model

1. **Per-path observation tracking**: `UiServer::pathObservations_` records the source (Request/WatchRefresh/IndexedMerge), status, and monotonic epoch for each path.
2. **Explicit failure outranks stale indexed success**: `MergeIndexDirectories` skips insertion when a newer explicit failure observation exists for the same canonical path (`FFDIAG_INDEXED_MERGE_SUPPRESSED`).
3. **Request failures published to snapshot**: `HandleRequestDirectory` now calls `RepublishAndBroadcastGeneration()` after storing failure status, so every subscribed client sees the terminal state.
4. **Terminal watcher refresh**: `DirectoryWatcher::processCompletion` invokes the change callback one final time before retiring on terminal error (`FFDIAG_WATCH_TERMINAL_REFRESH`).
5. **Terminal failure forces Unavailable**: `SnapshotFreshness::ForceUnavailable` sets `SnapshotViewState::Unavailable` regardless of LKG, and `ApplyColumnFailure` uses it for `NotFound`/`AccessDenied`/`PathMissing`.
6. **Recovery is explicit**: `AcceptGeneration` on a later successful generation clears the `Unavailable` state.

## Live UIA validation (2026-08-17, post-fix)

### Scenario: `in-column-error-state-late` — PASS

Exact assertions verified:
- Status text: `"This folder is no longer available."` — exact match
- ControlType: `Text`
- BoundingRectangle: `[224x40]` (non-zero, in-column)
- Pre-delete sentinel (`before-delete.txt`): absent from current children
- Held sentinel provider: stale (`UIA_E_ELEMENTNOTAVAILABLE` equivalent — `Get-UiaElementProperty` returns null)
- Recovery: new sentinel `after-recreate.txt` visible, old sentinel absent, unavailable status removed
- Old held provider: still stale after recreation (retarget blocked)

### 10x repeatability gate

| Run | Status | Duration (ms) |
|-----|--------|--------------|
| 1   | PASS   | 3551         |
| 2   | PASS   | 3235         |
| 3   | PASS   | 3255         |
| 4   | PASS   | 3269         |
| 5   | PASS   | 3275         |
| 6   | PASS   | 3200         |
| 7   | PASS   | 3240         |
| 8   | PASS   | 3330         |
| 9   | PASS   | 3261         |
| 10  | PASS   | 3437         |

**Result: 10/10 PASS, 0/10 FAIL, 0 `error-state-trigger-not-deterministic`**
Average duration: 3305ms

### Contamination checks

1. stable scenario → missing-path → stable scenario: PASS
2. missing-path → unrelated isolated scenario → missing-path: PASS

**Result: contamination-free**

### Full `windows-ui-automation-validation` capability

- **PASS (22):** launch-main-window, settings-dialog-structure, settings-dialog-close, keyboard-address-bar-navigation, column-populated-and-selected-item-in-tree, mouse-tab-operation, selection-and-scroll-patterns, scroll-pattern-real-viewport, home-surface-structure, storage-analysis-open, connection-badge-degraded, search-panel-open, search-results-discoverable, search-structured-filter-query, ui-responsive, rendering-geometry-click-precision, drag-between-panes, drag-out-to-explorer, in-column-error-state-late, search-deep-result-navigation, treemap-readability-click-precision, collect-evidence
- **SKIP (4), each naming a real independent prerequisite:**
  - `in-column-error-state` — `engine-connected-host`
  - `storage-mid-scan-calculating` — `privileged-scan-unavailable`
  - `search-toggle-service-while-open` — `privileged-scan-unavailable`
  - `drag-in-from-explorer` — `explorer-drag-source-unavailable`
- **FAIL (0)**

### Note on engine build

The initial live runs failed because the `build/debug-diagnostic` engine binary was stale (built 2026-08-16, before the `eea6922` engine fix). After rebuilding the diagnostic engine binary, the scenario passed deterministically. This confirms the engine fix (terminal watcher refresh + observation ordering) is the critical change, and the harness changes are correct.

## Validation matrix

### Focused tests
- `ffengine_directory_watcher_tests`: **PASS** — includes terminal-refresh and shutdown-no-fabricate tests
- `ffsnapshot_freshness_tests`: **PASS** — includes ForceUnavailable with LKG, older generation rejection, and regular Fail preservation

### Full Debug CTest
- **64/64 PASS**, 0 failures, 0 tests not run

### Build
- `cmake --preset debug && cmake --build --preset debug`: **PASS** (63/63 targets, zero warnings/errors)
- Compiler: MSVC `/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus`

### Policy/drift gates
- `check_source_markers.ps1`: **PASS** — no TEMP-DEBUG or DEBUG_STALL markers
- `check_diagnostic_policy.ps1`: **PASS** — release builds don't enable diagnostic escape hatch
- `ffdoc_drift_tests`: **PASS** — no doc drift, consistent line endings
- `git diff --check`: **CLEAN**

## Files modified

### Engine (path-state ordering and watcher terminal refresh)
- `src/engine/src/DirectoryWatcher.cpp` — terminal refresh callback (D4, task 3.2)
- `src/engine/src/UiServer.h` — PathObservation tracking structs (D2/D3, task 2.1)
- `src/engine/src/UiServer.cpp` — observation recording, merge gating, failure publication (D1-D3, tasks 2.1, 4.1)

### Protocol (diagnostics)
- `src/protocol/include/ffprotocol/Diagnostics.h` — `FFDIAG_WATCH_TERMINAL_REFRESH`, `FFDIAG_INDEXED_MERGE_SUPPRESSED`
- `src/protocol/src/Diagnostics.cpp` — code name entries

### UI (snapshot freshness and column failure)
- `src/ui/include/ffui/SnapshotFreshness.h` — `ForceUnavailable` method (D6, task 4.3)
- `src/ui/src/ColumnView.cpp` — `ApplyColumnFailure` uses `ForceUnavailable` for terminal states (D6, task 4.3)

### Tests
- `tests/engine/test_directory_watcher.cpp` — terminal-refresh and shutdown-no-fabricate tests (tasks 7.5, 7.6)
- `tests/ui/test_snapshot_freshness.cpp` — ForceUnavailable tests (tasks 7.1-7.4, 7.7)

### UIA harness (task 5.4, 6.1)
- `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1` — `Invoke-UiaScenarioErrorStateLate` rewritten: current-view-driven deletion, exact status text assertion, stale-provider proof, recovery verification, 10x repeatability via isolated lifecycle, no SKIP escape path
