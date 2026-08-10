# Watcher-safety acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

`ffengine_directory_watcher_tests` now covers the existing ordinary notification/restart behavior plus the recovery D5 bound. It creates 33 disposable directories, watches the first 32, refreshes root 0 to make root 1 least-recently-used, watches root 32, and verifies:

- a `state=watch-evicted` diagnostic is emitted;
- the diagnostic identifies root 1 as the evicted path;
- all watcher threads stop and the fixture is removed.

Focused result:

```text
ctest --test-dir build/debug -R ffengine_directory_watcher_tests --output-on-failure
100% tests passed, 0 tests failed out of 1
```

The implementation's overflow state machine remains intentionally unclaimed here. A reliable `ERROR_NOTIFY_ENUM_DIR` injection seam is not present, and a timing-only overflow stress test would be flaky; task 5.2 remains open pending deterministic fault injection or a bounded OS-level fixture.
