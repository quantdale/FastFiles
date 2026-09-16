# FastFiles real end-to-end verification

The E2E layer is a black-box test of the shipped user path using the local
diagnostic artifact:

```text
build\debug-diagnostic\src\ui\FastFiles.exe
```

It launches that executable, waits for the engine handshake and a usable UIA
Column View, performs UI Automation actions backed by real keyboard/mouse
input, and verifies filesystem effects. It does not call internal product
objects or replace directory results with mocks.

## Commands

```powershell
.\dev.ps1 test-e2e
.\dev.ps1 test-e2e -Filter launch
.\dev.ps1 test-e2e -Filter 'navigation,restore,search'
.\dev.ps1 test-e2e -Filter lifecycle -LifecycleIterations 5
.\dev.ps1 test-full
```

The CMake/Visual Studio tree also exposes an explicit `fastfiles_e2e` target
under `Tests/E2E`; it is opt-in and is not a default build or CTest dependency.

The direct runner is useful when selecting an explicit artifact or evidence
directory:

```powershell
pwsh -NoProfile -File .\verify\e2e\Run-FastFilesE2E.ps1 `
  -Exe .\build\debug-diagnostic\src\ui\FastFiles.exe `
  -Filter preview `
  -ArtifactsDir .\build\e2e-artifacts\preview-manual
```

`debug-diagnostic` is the intended local artifact because it accepts the
unsigned sibling engine through the existing debug-only discovery seam. The
normal `debug` and optimized `release-test` builds remain fail-closed for
production Authenticode pins; those profiles are covered by CTest and the
verification harness, not by silently weakening the E2E executable.

## Scenario contract

- `launch` proves more than a window handle: the real engine handshake and a
  usable semantic Column View must complete, with no indefinite Loading or
  Refreshing state.
- `navigation` opens the marker fixture, checks expected rows, nested browse,
  Back, Forward, Up, direct path entry, stale-column exclusion, and terminal
  status.
- `restore` closes after opening the nested fixture directory and cold-launches
  again in the same isolated state directory; the restored row must appear
  without an address-bar intervention.
- `search` checks exact, partial, no-result, rapid replacement/supersession,
  result selection, and opening a result through the visible Search panel.
- `storage` opens Storage Analysis, waits for a terminal state, rejects an
  infinite Calculating state, checks treemap/list UIA bounds against the live
  rendered content rectangle (with bounded DPI rounding tolerance), and
  invokes a real tile when data is available. An explicit degraded terminal
  state is accepted when exact aggregate data is unavailable.
- `preview` selects a real text file through pointer input, requires a
  terminal preview state and fresh reselection behavior, and checks the
  production Ready/Unsupported state surface rather than only a screenshot.
- `fileops` performs create-folder, rename, copy, move, and recycle-delete
  actions against disposable paths and verifies the resulting files on disk.
- `lifecycle` repeats launch/navigate/close with bounded waits and checks that
  repository-built FastFiles and FastFilesEngine processes and the control
  pipe do not remain behind.

## Fixtures and safety

Each run gets a unique marker-protected session under
`%LOCALAPPDATA%\FastFilesUiaSessions`. The file-operation scenario validates
every source and destination against that root before invoking the app. It
never targets the real Desktop, Documents, or repository. Cleanup is performed
by the UIA scenario context and refuses an unmarked or out-of-scope path.

For manual verification, `.\dev.ps1 fixture` creates the separate
`%TEMP%\FastFiles-dogfood` fixture, `.\dev.ps1 dogfood` starts a guided UI
session, and `.\dev.ps1 stop` stops only processes whose executable paths are
inside this repository's generated `build\` tree.

## Failure evidence and bounded waits

Artifacts are written below `build\e2e-artifacts\<run-id>\`, which is ignored
and is not source. Each scenario has deterministic timeouts. A failure records
the scenario/stage, expected and observed state, current visible location,
main UIA state, process IDs, a diagnostics-log tail, a UIA tree dump, and a
screenshot. The summary JSON preserves pass/fail/skip counts and the exact
executable path. A skipped run means interactive UI Automation is unavailable;
it is not reported as a product pass.

## Test classification

The fast C++ unit/component tests are registered with CTest under `tests/`.
The UIA-driver recorded-tree tests validate driver mechanics without launching
the product. The real E2E layer here is the only layer that proves the actual
executable's visible user flow. Privileged service/install, production signing,
clean-host, and live MFT/USN tests remain explicit external or opt-in gates.
