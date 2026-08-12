# Recover Degraded-Mode Browsing (Phase 0/1)

## Why

A forensic audit (2026-08-07) established that the development build cannot be used as a file manager. The single most visible blocker: launching `FastFiles.exe` from the build tree never brings up `FastFilesEngine.exe`. `EngineClient::LaunchEngineIfNotRunning` looks for the engine only in the UI executable's own directory (`src/ui/src/EngineClient.cpp:23-50`); the CMake build tree places the executables in sibling directories (`build/<config>/src/ui` vs `build/<config>/src/engine`), so `CreateProcessW` fails silently and the UI retries every 2 s forever — a window that appears alive while its backend is unreachable. Runtime reproduction confirmed this: UI responsive, no engine process; the engine launched directly from its build directory runs correctly.

Secondary audit findings block the same "usable basic file manager" milestone:

- Engine startup/connect failures are invisible (no bounded retry, no distinct states, no diagnostics).
- Snapshot publish failure (e.g. 2×8 MiB slot capacity exceeded) is logged but never surfaced to the client, which keeps stale data.
- `DirectoryWatcher` roots accumulate without bound, and a change-buffer overflow silently kills the watch.
- Search scope labels ("Current Drive", "All Indexed Locations") overstate the data search actually has (the snapshot of browsed directories), and the UI selects the broadest scope when the index service is active.

## What Changes

- Deterministic engine executable discovery: same directory as the UI (installed layout), sibling `engine` directory of the UI's parent (development layout), then `%ProgramFiles%\FastFiles` (installer layout). Validated before launch; never a PATH or disk-wide search.
- Bounded connection lifecycle in `EngineClient`: at most 5 attempts (~10 s) per cycle, then an explicit `Unavailable` state that waits for user retry instead of looping forever.
- Visible engine states on the existing status chip: `Starting engine…` / `Instant search: enabled` / `Instant search: basic (degraded mode)` / `Engine unavailable — click to retry`; the chip becomes the retry affordance for the unavailable state.
- Diagnostics for the resolved engine path, launch errors, and pipe-connection errors in `%LOCALAPPDATA%\FastFiles\logs\diagnostics.log`.
- Snapshot publish failure is surfaced to the requesting client as a new `DirectoryErrorReason::PublishFailed` (additive protocol enum value) with a clear per-column message instead of stale data.
- `DirectoryWatcher`: bounded root set (least-recently-watched eviction) and change-buffer-overflow recovery (detect, log, re-enumerate, re-arm; bounded retries).
- Honest search scope labels reflecting snapshot coverage.

## Capabilities

### New Capabilities
- `degraded-browsing-recovery`: launch reliability, connection-state visibility, and degraded-mode browsing hardening for the development build.

## Impact

- **Code**: `src/ui` (`EngineClient`, new `EngineDiscovery`, `WindowShell`, `ColumnView`, `SearchPanel`), `src/engine` (`UiServer`, `DirectoryWatcher`), an additive enum value in `src/protocol/include/ffprotocol/UiProtocol.h`, and a new test target `ffengine_discovery_tests`.
- **No changes** to the privileged path, signature pins, authentication, wire-format layouts, the security model, the index store, file operations, or the renderer.
- **Performance**: the connection lifecycle adds no steady-state cost; the watcher bound and overflow recovery prevent unbounded growth and silent loss of synchronization.
- **Compatibility**: no wire-format change (additive enum value only, both peers ship together); behavior on Windows 10/11 unchanged in kind.
