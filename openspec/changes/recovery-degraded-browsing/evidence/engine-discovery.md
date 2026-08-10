# Engine-discovery acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

## Deterministic candidate tests

```powershell
cmake --build --preset debug --target ffengine_discovery_tests
ctest --test-dir build/debug -R ffengine_discovery_tests --output-on-failure
```

`ffengine_discovery_tests` passed. The test covers installed-layout ordering, development sibling normalization, case-insensitive deduplication, `%ProgramFiles%` candidate derivation, `..` removal, missing-file rejection, directory rejection, and regular-file validation.

## Development-tree launch

The rebuilt `build\debug\src\ui\FastFiles.exe` was launched as a disposable process. It reached the sibling engine without PATH or disk search:

```text
ui_pid=36708
ui_live=True
engine_pid=34644
engine_live=True
engine_path=C:\Users\Michael Roy\Documents\FastFiles\build\debug\src\engine\FastFilesEngine.exe
ui_cleanup=graceful
engine_cleanup=stopped-by-probe
```

No FastFiles process remained after the probe.

## Missing/invalid-engine logging

Two disposable UI copies were run with an explicit isolated `ProcessStartInfo` environment overriding `LOCALAPPDATA`, `ProgramFiles`, `ProgramW6432`, and `ProgramFiles(x86)`. The first had no engine candidate; the second had a regular-file, non-PE `FastFilesEngine.exe` fixture. Neither touched the installed layout.

The no-candidate run kept the UI alive after the retry window and logged `state=engine-connect-failed` followed by `state=engine-unavailable`. The invalid-candidate run produced:

```text
engine_launch_failed_events=1
engine_connect_failed_events=5
engine_unavailable_events=1
state=engine-launch-failed ... error=0xd8
state=engine-unavailable ... error=0x0
ui_cleanup=graceful
probe_removed=True
```

The event names are stored in the diagnostic `state=` field by the existing implementation; the OS error is retained in `error=`. Both probe directories and processes were cleaned up.

## Gate result

The final Debug baseline discovered 42 tests and passed 42/42; the Analyze build also passed. Tasks 1.1–1.3 and 7.1–7.2 are evidenced by the source seams, focused test, runtime probes, and full gates. Recovery task 7.3 (deep navigation, file opening/search, and interactive retry click) remains open because this slice did not fabricate that broader acceptance.
