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

## Post-fix ordering model

1. **Per-path observation tracking**: `UiServer::pathObservations_` records the source (Request/WatchRefresh/IndexedMerge), status, and monotonic epoch for each path.
2. **Explicit failure outranks stale indexed success**: `MergeIndexDirectories` skips insertion when a newer explicit failure observation exists for the same canonical path (`FFDIAG_INDEXED_MERGE_SUPPRESSED`).
3. **Request failures published to snapshot**: `HandleRequestDirectory` now calls `RepublishAndBroadcastGeneration()` after storing failure status, so every subscribed client sees the terminal state.
4. **Terminal watcher refresh**: `DirectoryWatcher::processCompletion` invokes the change callback one final time before retiring on terminal error (`FFDIAG_WATCH_TERMINAL_REFRESH`).
5. **Terminal failure forces Unavailable**: `SnapshotFreshness::ForceUnavailable` sets `SnapshotViewState::Unavailable` regardless of LKG, and `ApplyColumnFailure` uses it for `NotFound`/`AccessDenied`/`PathMissing`.
6. **Recovery is explicit**: `AcceptGeneration` on a later successful generation clears the `Unavailable` state.

## Validation matrix

### Focused tests
- `ffengine_directory_watcher_tests`: **PASS** — includes terminal-refresh and shutdown-no-fabricate tests
- `ffsnapshot_freshness_tests`: **PASS** — includes ForceUnavailable with LKG, older generation rejection, and regular Fail preservation

### Full Debug CTest
- **64/64 PASS**, 0 failures, 0 tests not run
- Duration: 46.52 seconds

### Build
- `cmake --preset debug && cmake --build --preset debug`: **PASS** (63/63 targets, zero warnings/errors)
- Compiler: MSVC `/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus`

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
