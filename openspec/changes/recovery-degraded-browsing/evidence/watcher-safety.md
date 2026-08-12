# Watcher-safety acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

`ffengine_directory_watcher_tests` covers the existing ordinary notification/restart behavior, the recovery D5 bound, and deterministic overflow recovery. It creates 33 disposable directories, watches the first 32, refreshes root 0 to make root 1 least-recently-used, watches root 32, and verifies:

- a `state=watch-evicted` diagnostic is emitted;
- the diagnostic identifies root 1 as the evicted path;
- all watcher threads stop and the fixture is removed.

The watcher test target defines `FF_DIRECTORY_WATCHER_TESTING`, which injects
completion results only into that test target. The production target continues
to call `ReadDirectoryChangesW`; both paths share the same completion-processing
lambda. The deterministic overflow cases verify:

- a zero-byte completion is treated as `ERROR_NOTIFY_ENUM_DIR`;
- an explicit `ERROR_NOTIFY_ENUM_DIR` invokes the change callback and re-arms;
- a successful completion resets the consecutive-overflow counter;
- eight consecutive overflows invoke the callback eight times, log
  `state=watch-gave-up`, and stop the watch.

Focused result:

```text
ctest --test-dir build/debug -R ffengine_directory_watcher_tests --output-on-failure
100% tests passed, 0 tests failed out of 1
```

Final focused validation also passed `ffdoc_drift_tests`; the full Debug and
Analyze builds and serial CTest passed 42/42. The overflow fixture uses only
disposable directories and restores `LOCALAPPDATA` before cleanup.
