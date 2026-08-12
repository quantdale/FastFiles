# FAST FILES — REPOSITORY INTELLIGENCE & RECOVERY REPORT

*Forensic engineering audit. Read-only investigation: no source files modified, nothing committed, no system changes. 13 parallel investigation agents + one build/run agent (2026-08-07). Every significant claim carries evidence (`path:file:line` or symbol) and a confidence label: CONFIRMED / LIKELY / POSSIBLE / UNKNOWN. This file is a new, untracked deliverable — not part of the source tree.*

---

## 1. Executive Summary

FastFiles is a Windows-only native C++20 file manager built around a deliberate, unusual architecture: a **three-process privileged/unprivileged split**. A LocalSystem Windows service (`FastFilesIndexSvc`) is a stateless, narrowly-scoped broker that reads raw NTFS metadata (MFT records and the USN Journal via `DeviceIoControl`) and streams bytes to an unprivileged per-logon-session engine (`FastFilesEngine`), which owns a SQLite-backed filesystem index and publishes an immutable, memory-mapped "snapshot" that the Direct2D desktop UI (`FastFiles`) reads entirely in-process. The goal is WizFile-class instant search, WizTree-class storage analysis, and a macOS-Finder-style Column View, in a modern Fluent-style shell.

**A very large amount of real, well-engineered code exists.** The build is green (zero warnings under `/W4 /WX`), 29 tests pass (2 flaky under parallelism), the app launches to a live, responding window, and the degraded-mode browsing pipeline, file operations (`IFileOperation` with undo/conflict handling), drag-and-drop, preview, theming, command palette, and settings are genuinely implemented and unit-tested. This is not a repo of stubs.

**Why it is still not usable as a file manager** — four compounding root causes:

1. **The product's core differentiator — the privileged NTFS index — is inert by design.** The mutual-authentication signature pins are all-zero placeholders (`src/setup/include/ffsetup/PinnedSignatures.h:16-17`), so every peer is rejected at handshake and **no MFT/USN scan has ever run end-to-end**. The engine therefore has no index data, and every feature that depends on the index (full-disk instant search, drive-wide storage analysis) has no substrate.
2. **Search silently shrinks to "folders you have browsed".** Search is a pure in-process scan of the snapshot (`ExecuteSearch`, `src/search/src/Search.cpp:83`); there is no search command on the UI↔engine pipe (`UiProtocol.h` has none). In the active degraded mode the snapshot only contains directories the user visited, so "Current Drive / All Indexed Locations" scopes are aspirational labels over a sliver of the disk.
3. **The storage-analyzer treemap is a stub, and the analyzer's list views cannot render.** `TreemapView` gives every directory size 0 (`TreemapView.cpp:106-107`) and has no folder-aggregate plumbing; the drill-down/list views never receive `WM_NOTIFY` because `WindowShell` routes it only to SearchPanel (`WindowShell.cpp:1147-1149`) — `LVS_OWNERDATA` lists with no `LVN_GETDISPINFOW` render empty.
4. **Silent failure everywhere.** Startup failures exit with code 1 and no dialog; a missing engine is retried every 2 s forever with no user-visible error; oversize snapshot publishes fail silently leaving stale data; watcher overflows kill the watch silently; and there is no staleness detection. The user gets a window that looks alive but is quietly empty or stale.

Secondary but significant: UI state is fragmented across 8 sync sites (history corruption, broken tab restore), the dual-pane toggle is dead, and the engine is not launched when running from a build tree because the UI looks for `FastFilesEngine.exe` in its own directory (observed: window open, no engine process).

**Recovery verdict:** nothing needs to be rewritten. The fastest path to a usable file manager is a **degraded-mode-first vertical slice** (reliable engine startup, honest search scopes, analyzer list/treemap wiring, state consolidation), followed by a decision on the privileged path: provision real signature pins, or give the engine an unprivileged full-tree indexer that feeds the existing store.

---

## 2. Product Vision Reconstructed From Repository

From `README.md`, `openspec/changes/*/proposal.md`, and `AGENTS.md`, the repository indicates FastFiles is intended to become:

- **A WizFile-class instant filename search** over a persistent NTFS index (MFT + USN Journal), with zero-IPC keystroke search via a memory-mapped snapshot (specs `search-query-engine`, `search-ui`, `index-engine`, `filesystem-index-store`, `privileged-index-service`).
- **A WizTree-class storage analyzer**: volume overview, drill-down, largest files/dirs, file-type categories, squarified treemap (`storage-overview`, `storage-drilldown-and-treemap`, `file-type-categorization`).
- **A Finder-style Column View** with tabs, dual pane, address bar, sidebar, bookmarks (`column-view-browsing`, `navigation-and-workspace`).
- **A modern Windows UI**: Fluent design tokens, dark/light theme, animations gated on system settings, high-DPI, keyboard-first (`ui-appearance`, `theming`, `keyboard-shortcuts`).
- **Full file operations** via `IFileOperation` with conflict resolution, progress, undo, multi-select, OLE drag-drop (`file-operations-core`, `conflict-resolution`, `multi-selection-and-dragdrop`).
- **A hardened security posture**: privileged access only through a constrained, stateless, mutually-authenticated broker; degraded mode (unprivileged enumeration) as a first-class permanent state.
- Tooling: OpenSpec spec-driven workflow, a PowerShell verification harness (`verify/`), autonomous engineering loop (`verify/intake.ps1`).

---

## 3. Repository Map

| Directory | Responsibility |
|---|---|
| `src/` | First-party C++20; one CMake target (or small group) per component, public headers in `include/ff<name>/` |
| `src/protocol/` (`ffprotocol`) | Wire protocol: frames, closed command enums (service + UI seams), MFT/USN record layouts, dispatch, versioning, snapshot format, settings schema, diagnostics. The leaf everything depends on |
| `src/ipc/` (`ffipc`) | Named-pipe framing (`PipeFraming`) and per-connection accept loop (`PipeListener`) |
| `src/setup/` (`ffsetup`) | Privileged install-time ops: service registration, `FastFilesUsers` group, install-dir ACL, security descriptors, scheduled task, Authenticode verification + **signature pins** |
| `src/indexstore/` (`ffindexstore`) | SQLite durable store (`Store`) + in-memory projection (`Projection`), interned name pool, flat hash maps |
| `src/search/` (`ffsearch`) | Query parsing (`Query`), in-memory scan (`Search`), history |
| `src/indexsvc/` | 3 targets: `ffmftparser` (raw MFT byte parser), `ffindexsvcprobes` (privilege/volume/auth probes), `FastFilesIndexSvc` exe (service: `ServiceConnection`, `VolumeScanner`, `UsnJournalReader`, `ConnectionRegistry`, `StalenessMonitor`, `ClientAuthentication`) |
| `src/engine/` | `FastFilesEngine` exe: `IndexPipeline` (store+projection owner), `VolumeSessionManager` (orchestrator), `PrivilegedConnection` (service client), `UiServer`, `SnapshotPublisher`, `DegradedModeEnumerator`, `DirectoryWatcher`, `IdleManager` |
| `src/ui/` | `FastFiles` WIN32 GUI: `WindowShell`, `ColumnView`, `NavigationWorkspace`, `NavigationChrome`, `NavigationSidebar`, `SearchPanel`, `CommandSystem`/`CommandPalette`, `FileOperations`/`FileOperationPolicy`/`ConflictDialog`, `OleDragDrop`, `Preview`, `StorageAnalysis`/`TreemapView`/`CategoryEngine`, `Renderer`, `EngineClient`, `IconCache`, `UITheme`/`UiStyle`/`UiAnimation`, `SettingsDialog`, `SelectionModel`, `QuickActions`, `Util` |
| `src/installer/` | `FastFilesSetup` exe: `InstallSteps` (service/group/ACL/task), scratch-dir TOCTOU defense, UAC relaunch |
| `src/fftest/` | Privilege-diagnostics probe binary against the live service |
| `tests/` | Plain C++ CTest executables (mirror components) + PowerShell-gated tests (`uia-driver`, `drift`) |
| `verify/` | PowerShell runtime-verification harness (`verify.ps1`, `intake.ps1`), UIA driver. **`runs/`, `baselines/`, `.signing/` untracked execution evidence — not read per policy** |
| `openspec/` | Spec-driven workflow: `config.yaml`, 28 top-level capability specs, 15 changes (+ empty `archive/`) |
| `third_party/sqlite/` | Vendored SQLite amalgamation 3.53.4 (compiled as C, `/W0`, `SQLITE_THREADSAFE=1`, `OMIT_LOAD_EXTENSION`) |
| `build/` | CMake preset trees (`debug`/`release`/`analyze`/`asan`/…) + `ffbuild*.bat`, `ffctest_vs.bat` |
| `dist/` | Deployment artifact: `FastFiles.exe` (862,208 B, 2026-08-04) — **stale vs. current build** |
| `codebase-audit/` | Prior audit records (prompt + session logs) |
| `.github/` | Single CI workflow: windows-latest, matrix `[debug, release, analyze]`, configure+build+ctest |
| Root docs | `AGENTS.md`, `CLAUDE.md`, `AUTONOMOUS.md`, `CODE_INDEX.md` (verified accurate), `README.md`, `progress.md` |

**CMake targets** (verified against all component CMakeLists; root flags `/W4 /WX /permissive- /sdl /guard:cf`, link `/DYNAMICBASE /NXCOMPAT /guard:cf`, `_WIN32_WINNT=0x0A00`, C++20):

- Static libs: `ffprotocol`, `ffipc`, `ffsetup`, `ffindexstore` (links `sqlite3`), `ffsearch`, `ffmftparser`, `ffindexsvcprobes`.
- Executables: `FastFilesIndexSvc` (console/service), `FastFilesEngine` (console), `FastFiles` (WIN32 GUI), `FastFilesSetup`, `fftest`.
- 26 C++ test executables + 3 PowerShell-gated tests (29 total when `pwsh` present). `ffindexstore_bench_projection_memory` is deliberately **not** registered with CTest.

---

## 4. Technology Stack (evidence)

| Technology | Evidence |
|---|---|
| C++20, MSVC `/W4 /WX` | `CMakeLists.txt:10-12,32-35` |
| Direct2D/DirectWrite/DirectComposition/D3D11/DXGI | `src/ui/CMakeLists.txt:36-40`; `Renderer.cpp:13-81`; `ColumnView.h:9`; `UITheme.h:12` |
| COM (`IFileOperation`, WIC, Task Scheduler) | `FileOperations.cpp:316-317,211`; `IconCache.cpp:198`; `ScheduledTaskRegistration.cpp:27,118` |
| Win32 FS/volume APIs | `DegradedModeEnumerator.cpp:17` (`FindFirstFileExW`); `DirectoryWatcher.cpp:41` (`ReadDirectoryChangesW`); `VolumeScanner.cpp:116-208`, `UsnJournalReader.cpp:68-184` (`CreateFileW(\\.\X:)` + `DeviceIoControl`) |
| Named pipes | `PipeListener.cpp:21,40` (`CreateNamedPipeW`/`ConnectNamedPipe`) |
| SQLite 3.53.4 (vendored, WAL) | `third_party/sqlite/sqlite3.h:149`; `Store.cpp` |
| Authenticode / WinVerifyTrust | `AuthenticodeVerification.cpp:54,63`; links wintrust crypt32 |
| Task Scheduler COM | `ScheduledTaskRegistration.cpp:117-118` |
| SCM / Advapi32 | `ServiceRegistration.cpp:186,206`; `InstallSteps.cpp:110` |
| Memory-mapped snapshot | `SnapshotPublisher.cpp:22,30`; `EngineClient.cpp:52-67` |
| Testing | Hand-rolled `Check()` (`tests/TestSupport.h`), CTest, no gtest/catch |
| No managed code, no WinUI/WPF/XAML, no third-party UI libs | confirmed by CMake links and headers |

---

## 5. Application Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    UI  (FastFiles.exe)                       │
│  WindowShell (HWND, message loop, theme, device-loss)        │
│  ├─ ColumnView (Direct2D column browser, SelectionModel)     │
│  ├─ NavigationWorkspace (tabs, panes, history, address bar)  │
│  ├─ SearchPanel (debounce → ffsearch over snapshot)          │
│  ├─ StorageAnalysis (overview/drill/categories/treemap)      │
│  ├─ FileOperations (IFileOperation STA worker, undo)         │
│  ├─ Preview / CommandPalette / SettingsDialog / IconCache    │
│  └─ EngineClient (control-pipe client + snapshot mapper)     │
└───────────────┬──────────────────────────────────────────────┘
                │ named pipe \\.\pipe\FastFiles.Ui.Ctrl.<sid>
                │ + shared memory Local\FastFiles.IndexSnapshot.<sid>
┌───────────────▼──────────────────────────────────────────────┐
│       ENGINE  (FastFilesEngine.exe, unprivileged)            │
│  UiServer (per-connection handler, degraded enumerator,      │
│            directory watcher, snapshot publish)              │
│  IndexPipeline (SQLite Store + Projection, single mutex)     │
│  VolumeSessionManager (scan/journal/reconcile orchestration) │
│  PrivilegedConnection (service client, heartbeat/backoff)    │
│  SnapshotPublisher / IdleManager                             │
└───────────────┬──────────────────────────────────────────────┘
                │ named pipe \\.\pipe\FastFiles.Svc.Ctrl
                │ (mutual auth: image path + pinned signature)
┌───────────────▼──────────────────────────────────────────────┐
│   SERVICE  (FastFilesIndexSvc.exe, LocalSystem)              │
│  ServiceConnection (per-connection, closed command surface)  │
│  VolumeScanner (MFT) / UsnJournalReader (USN Journal)        │
│  ConnectionRegistry / ClientAuthentication / StalenessMonitor│
│  ffmftparser (raw MFT record parsing, no $DATA ever)         │
└──────────────────────────────────────────────────────────────┘
```

Two named-pipe seams only (by design, one kernel-object type to ACL). The UI↔engine seam is same-privilege control plane; the engine↔service seam is the elevation boundary. Search reads the snapshot in-process — zero IPC per keystroke.

---

## 6. Application Startup Sequence

**UI (`src/ui/src/Main.cpp:12`)** — `wWinMain` → DPI awareness → `OleInitialize` → `WindowShell::Initialize`: register class → `CreateWindowExW` (1024×640) → `Renderer::Initialize` (D3D11 → D2D → DWrite → DComp swapchain) → load `%LOCALAPPDATA%\FastFiles\settings.json` → `ApplyTheme` → `ColumnView::Initialize` (column 0 = fixed drives only) → panels → `RegisterDragDrop` → `EngineClient::Start` (threaded, never fails) → `ShowWindow` → message loop.

`EngineClient` management thread: `LaunchEngineIfNotRunning` (`EngineClient.cpp:23-50`) — if the UI pipe is unreachable, `CreateProcessW` launches **`FastFilesEngine.exe` from the same directory as the UI exe**; on failure it retries every 2 s **forever** with no log and no UI error. When connected: subscribe → map snapshot section → badge shows "Instant search: basic — click to enable" in degraded state (`ColumnView.cpp:651`).

**Engine (`src/engine/src/Main.cpp:96`)** — register scheduled task (best-effort) → `UiServer::Start` (snapshot mapping + current-user pipe ACL + pipe bind) → **exits with code 1 if the UI pipe cannot bind** (then the UI relaunches it every 2 s) → open `%LOCALAPPDATA%\FastFiles\index.db` with `PRAGMA integrity_check` (failure → delete DB and rebuild fresh; still unopenable → degraded session, stderr only) → `LoadSettings` → `VolumeSessionManager.Start` (reconciliation thread) → `RebuildAll` (re-export persisted volumes into the snapshot) → `PrivilegedConnection.Start` (background: connect → verify peer image + pinned signature → handshake; service absent → backoff 1 s→60 s, retry forever) → park on an event (`Main.cpp:235-239`). **No startup timeout anywhere.**

**Service (`src/indexsvc/src/Main.cpp`)** — SCM `ServiceMain`: DLL-search-path hardening → admin-only log/crash-dump dirs → loaded-binary hash + staleness monitor → backup-privilege probe (logs only, non-fatal) → resolve `FastFilesUsers` group → build pipe ACL → bind `\\.\pipe\FastFiles.Svc.Ctrl`; missing group or ACL failure → **refuses to start** (fail closed). No standalone console mode.

**Failure points where a silent/empty UI results** (CONFIRMED): any of the ~10 renderer init steps, class/window registration, `OleInitialize`, panel inits → `exit 1` with **no dialog**; engine missing/dead → window opens, only drive roots, 2 s relaunch loop; snapshot publish overflow → stale data, no error; index DB open failure → degraded, stderr only (invisible when launched from GUI).

---

## 7. Runtime Investigation (2026-08-07, non-admin shell)

- **Build:** `cmake --build --preset debug` → `ninja: no work to do`, exit 0. Fully up to date, no warnings/errors. Environment gaps (cmake/ninja not on PATH; VS-bundled copies used) are environment, not repository.
- **Tests:** 29 tests, ~13 s suite, no hangs. Runs: 27/29, 28/29, 29/29, 28/29, 28/29. **Two flaky timing-sensitive tests under `-j4`**: `fffileoperations_tests` ("custom conflict dialog result wrong") and `ffpreview_tests` ("delivered a superseded result"; "rapid-burst … deadline"). Both pass in isolation. Classification: REPOSITORY (test races), not environment.
- **Launch:** `build/debug/src/ui/FastFiles.exe` → window present, title "FastFiles", `Responding=True` at +12 s/+20 s, no crash, no WER/Application-Error events. **No `FastFilesEngine` process at any check** — CONFIRMED consistent with `LaunchEngineIfNotRunning` looking for the engine in the UI's own directory (`EngineClient.cpp:38-40`); in the build tree the engine is in `build/debug/src/engine/`, not `build/debug/src/ui/`. In an installed layout all exes are staged into one directory, so this is a dev-layout failure mode. UNKNOWN whether a stale `dist/FastFiles.exe` (2026-08-04) launch behaves identically.
- **Service:** `FastFilesIndexSvc` process running (pre-existing); state unverifiable from non-admin shell (SCM query access denied). It accepts no clients (placeholder pins).
- **Logs:** `%LOCALAPPDATA%\FastFiles\logs\diagnostics.log` got a fresh startup entry `state=startup-token outcome=backup-privilege-unavailable`; `index.db`, `settings.json`, `workspace-state.json` present. No error-level entries.

**Verdict:** the app launches and renders, but on this machine the engine was never reached, so the observed session could not have shown any directory contents beyond the drive list.

---

## 8. Minimum File Manager Workflow

| Step | Status | Evidence |
|---|---|---|
| Launch application | WORKING (window appears, responds) | runtime §7 |
| Display filesystem | PARTIALLY WORKING — drive list renders (column 0); engine-dependent columns need the engine, which failed to spawn from the build tree | `EngineClient.cpp:23-50`; runtime observation |
| Navigate into directory | PARTIALLY WORKING — pipeline is real and unit-tested; blocked in practice by engine reachability | §9 |
| Navigate backward/upward | WORKING (history in NavigationWorkspace; arrow keys in ColumnView; no Backspace handler) | `ColumnView.cpp:147-202` |
| Select file/folder | WORKING (set-based selection, Ctrl/Shift) | `SelectionModel.h:8-39` |
| Open file/folder | WORKING (`ShellExecuteW`; folders navigate in-app) | `QuickActions.cpp:117` |
| Search | PARTIALLY WORKING — works, but only over the snapshot of browsed directories; scope labels overstate coverage | §10 |
| Display results | WORKING (virtualized owner-draw list) | `SearchPanel.cpp` |

Secondary ops (copy/move/rename/delete/create/undo/conflict/progress/drag-drop): implemented and e2e-tested (§13), though not exercised in the live GUI session.

---

## 9. File Browsing Architecture

```
✓ UI input (mouse/keyboard) → ColumnView::OnMouseDown / OnKeyDown
✓ ActivateItem → TruncateAfter → RequestColumn (ColumnView.cpp:104-143)
✓ EngineClient::RequestDirectory (queued, never blocks UI; EngineClient.cpp:365-404)
✓ UiServer::HandleRequestDirectory — SYNCHRONOUS on per-connection thread
  (UiServer.cpp:239-269; PipeListener spawns a detached thread per client, PipeListener.cpp:81-82)
✓ DegradedModeEnumerator::EnumerateDirectoryDegraded (FindFirstFileExW, single level,
  no recursion; DegradedModeEnumerator.cpp:10-44)
✓ Full snapshot reserialize + double-buffered slot flip + generation++ (SnapshotPublisher.cpp:56-82)
✓ UI notified via 8-byte NewGeneration pipe frame (UiServer.cpp:108-116)
✓ EngineClient parses snapshot (mapped once, never re-mapped; EngineClient.cpp:52-92)
✓ ColumnView::RefreshColumnFromSnapshot — in-process sort, selection preserved (ColumnView.cpp:430-503)
✓ Direct2D render, columns culled by X, rows draw-skipped off-screen (ColumnView.cpp:674-676,701-705)
```

Details (all CONFIRMED): `FindFirstFileExW` with flags=0 — no `LARGE_FETCH`, no `ON_DIRECTORY`; hidden/system/reparse files are **not** filtered (attributes captured; reparse points never followed); access-denied only detected if the top-level call fails — subdirectory access is never probed; no `\\?\` long-path prefix and no manifest (paths >~260 chars LIKELY fail); no enumeration cancellation; `entry.accessible=true` hardcoded (`DegradedModeEnumerator.cpp:33`) despite the header contract.

**DirectoryWatcher** (`DirectoryWatcher.cpp`): one thread + 64 KiB buffer per watched root, `bWatchSubtree=FALSE`, **unbounded `std::map` of roots** (`DirectoryWatcher.h:37`) — every requested path becomes a permanent watch. `ERROR_NOTIFY_ENUM_DIR` (buffer overflow) **silently kills the watch** with no re-watch (`:44-46`). Watches persist for the engine lifetime.

**Failure modes:** oversize publish (>8 MiB) → no generation, **no error reply** (`SnapshotPublisher.cpp:62-64`, `UiServer.cpp:102-106`) — client keeps stale data with no indication; no staleness detection anywhere (`ReadSnapshot` only fails if unmapped, `EngineClient.cpp:83-85`).

---

## 10. Search Architecture

### Search source
`ffsearch` is a **pure in-memory linear scanner** over a caller-supplied flat `std::vector<Candidate>` (`ExecuteSearch`, `Search.cpp:83`). **Single caller in the entire tree: `SearchPanel::WorkerMain`** (`SearchPanel.cpp:365`). There is no search over the SQLite index and no recursive-walk search. `Candidate` = name/folder/size/times/isDirectory/volumeId/id/parentId (`Query.h:14`) — note file-ID High halves are dropped by the snapshot plumbing.

### Query pipeline
```
✓ keystroke → CBN_EDITCHANGE → 125 ms debounce timer (SearchPanel.cpp:284-291)
✗ Dispatch [UI thread]: PopulateCandidates = full ParseSnapshot + flatten of the ENTIRE
  snapshot map → vector<Candidate> on the UI thread per keystroke (SearchPanel.cpp:738-776)
✓ ParseQuery (filters ext/name/folder/size/modified/kind, globs, ordinal contains)
✗ RecordHistory — synchronous file rewrite + combobox rebuild per keystroke (UI thread)
✓ worker thread ExecuteSearch (generation-cancelled per 1024 candidates)
✓ PostMessage WM_APP_SEARCH_COMPLETE → virtualized owner-draw results list
✓ double-click → ReconstructPath (walks parentId links within the snapshot set)
```
- **No search message exists on the UI↔engine protocol** — CONFIRMED: all 16 `UiMessageType`s are control-plane (`UiProtocol.h:15-32`). Search is 100% snapshot-local by design.
- Scope labels: `SearchScope` (CurrentFolder/+Subfolders/CurrentDrive/AllIndexedLocations, `Search.h:13`) are string-prefix checks over `candidate.folder`. "Current Drive" / "All Indexed Locations" are force-disabled without the engine (`SearchPanel.cpp:294-296`).

### Index lifecycle
- DB: `%LOCALAPPDATA%\FastFiles\index.db` (WAL; integrity check on open; corrupt → delete+rebuild). Created/owned by the engine.
- Ingestion (privileged path only): `VolumeSessionManager` → `PrivilegedConnection` callbacks → `IndexPipeline::ApplyMftBatch/ApplyUsnBatch` (store-before-projection) → **full-volume `ExportDirectorySnapshot` + republish on every batch** (`VolumeSessionManager.cpp:358-362,434-438,473-481`) → snapshot.
- Change detection: USN journal batches + 6-hourly reconciliation (privileged); `ReadDirectoryChangesW` for UI-browsed roots only (degraded). Index-exported directories are **not** watched.
- **But the privileged path is unreachable**: placeholder pins → mutual auth fails closed → no scan/journal/volumes ever. `RebuildAll` runs at startup and `volumes.available` defaults to 1 (`Store.cpp:24,300`), so the engine re-exports *persisted* volumes from the DB — but since no scan ever succeeded, **the DB is empty in practice; degraded search sees only browsed directories + stale last-session data**.

### Performance problems (all CONFIRMED in code)
1. Full snapshot re-parse on the **UI thread** per debounced keystroke (the expensive half of search).
2. **Whole-index export per batch** — O(index) work per ingestion batch, on the service-reader thread, under the pipeline mutex (≈O(N²) during an initial scan).
3. **8 MiB slot cap vs. a real volume** — `SnapshotFormat.h:21`; a full C: export exceeds it, `Publish` returns 0, generation never advances → search silently operates on a stale/empty snapshot (LIKELY broken at scale).
4. Unbounded results (no cap; stored twice), O(n·m) matcher with per-candidate allocations, no incremental filtering.
5. Per-keystroke history file rewrite.

### Disconnects
- `defaultSearchScope` setting is dead — written/read but never consumed by SearchPanel (hardcodes scope when engine active). CONFIRMED.
- Snapshot header comment describes an outdated layout (code serializes times/IDs; comment omits them). CONFIRMED drift.
- Tests cover the pure search layer only; nothing covers `PopulateCandidates`, snapshot integration, or the 8 MiB bound.

---

## 11. Storage Analyzer Architecture

```
[Scanner]   MFT/USN privileged ✓ (inert — pins) | per-browse FindFirstFileEx △
[Model]     SQLite Store + Projection ✓ (rebuilt at startup)
[Aggregate] Projection walk ✓ (indexed) | ✗ degraded (no FRNs) | Store recursive CTE ✗ dead on wire
[Visualize] TreemapView (Direct2D squarify) ✗ — directories have no sizes
[UI]        Overview ✓ (GetDiskFreeSpaceEx) | Drill-down list ✗ cannot render | Categories ✓ | Treemap ✗
```

- **Aggregate request path is real**: `EngineClient::RequestFolderAggregate` (`EngineClient.cpp:483`) → `UiServer::HandleRequestFolderAggregate` (`UiServer.cpp:271`) → `IndexPipeline::GetFolderAggregate` (`IndexPipeline.cpp:417`) → `Projection::GetFolderAggregate` (iterative parent-FRN walk). The SQLite recursive CTE (`Store::GetFolderAggregate`, `Store.cpp:623`) is **only called by unit tests** — dead on the wire.
- **Treemap stub (core gap):** `TreemapView::BuildTree` sets `totalSizeBytes = isDirectory ? 0 : sizeBytes` (`TreemapView.cpp:106-107`) and has **no aggregate-request plumbing** — no `RequestFolderAggregate` calls, no result handler. Squarify therefore treats every directory as 0 bytes → clamped to 4 px slivers (`TreemapLayout.h:20,124`), rendered in the "calculating" color forever (`TreemapView.cpp:164`). Effectively a files-only tile map of the current folder.
- **Degraded mode: aggregates structurally impossible** — `RequestAggregateForItem` skips `volumeRowId == 0` (`StorageAnalysis.cpp:458`) and degraded entries always have `volumeRowId = 0` (`DegradedModeEnumerator.h:7-15`, `UiServer.cpp:22-37`). Subtree sizes stay "Calculating…" forever.
- **Lists cannot render (major UI bug):** `WindowShell` routes `WM_NOTIFY` only to SearchPanel (`WindowShell.cpp:1147-1149`); `StorageAnalysis::HandleNotify` is never called, yet its list views are `LVS_OWNERDATA` — no `LVN_GETDISPINFOW` ⇒ **empty lists; drill-down, largest-files, sort all dead**. Also `HandleTimer`/`kRefreshTimer` dead.
- **Entry point is palette-only:** `storage.analyze` command exists (`CommandSystem.cpp:205`), no shortcut, **not in the menu bar**. Reachable only via Ctrl+Shift+P.
- **Paper-vs-code drift:** `tasks.md` 4.3 (catch-all rectangle — absent in code), 4.5 (hover tooltip — only a highlight ring), 7.3/7.4 (treemap visual SKIPPED/never validated). All 38 tasks marked `[x]`.
- No standalone full-drive scanner exists outside the indexed path.

**Verdict: NOT end-to-end.** Working: volume overview (OS APIs) and — *only when the volume is indexed* — drill-down aggregates. Everything else is disconnected: treemap↔aggregates, list rendering, degraded mode, menu access.

---

## 12. Column View Architecture

- **Model:** `std::vector<Column> columns_` (+ `columns2_` for pane 2); `Column = {path, vector<ColumnItem>, selectedIndices, anchor, focusIndex, error}` (`ColumnView.h:53-60,158-159`). Entries are **copied** out of the snapshot per refresh (`RefreshColumnFromSnapshot`, `ColumnView.cpp:430-481`), sorted folders-first. Column 0 is a synthetic drive list.
- **Drill:** select folder → `ActivateItem` → `TruncateAfter` (destroys deeper columns) → push empty `Column{path}` + `RequestDirectory` → engine publishes new generation → `OnSnapshotUpdated` populates. Files never create columns.
- **Path state:** per-column `path`; `ActivePanePath()` = focused column's path (`:409-417`). **No history in ColumnView** — back/forward lives in NavigationWorkspace; **no `VK_BACK` handler exists**.
- **Virtualization:** columns culled by X; rows iterate all items and draw-skip — O(items)/column/frame.
- **Redraw:** on every `WM_MOUSEMOVE` (`WindowShell.cpp:1391-1402`), selection, wheel, all WM_APP events; no dirty-flag batching.
- **Keyboard:** two-stage — pre-translate `DispatchShortcut` (Global→ActiveView, `WindowShell.cpp:643-649`) takes precedence over native `OnKeyDown`. **Ctrl+A is unreachable in ColumnView** because the chord dispatcher consumes it first. Ctrl+C/V/Delete while typing in an edit box fire FastFiles commands instead of edit-box operations (LIKELY user-visible bug).
- **Dual pane:** full machinery exists (`columns2_`, `SetDualPane`, `ActivatePane`) but **`SetDualPane` has zero callers** — the toggle command only flips NavigationWorkspace's panes; the `navigation.toggle-column-view` command is a **verbatim copy of `toggle-dual-pane`** (`WindowShell.cpp:323-327`). Both toggles are effectively inert.
- **Renderer:** hardware D3D11 BGRA, `CreateSwapChainForComposition` FLIP_SEQUENTIAL, DComp visual; device loss → `RecreateSwapChainAndTarget` + `MarkDeviceDirty` fan-out (`Renderer.cpp:104-157`, `WindowShell.cpp:828-831`). Single-threaded D2D; one `Present(1,0)` per paint; text via cached `IDWriteTextFormat`s (no `IDWriteTextLayout` anywhere). Render thread = UI thread.
- **Theming:** single design-token set (`UITheme.h`: 34 colors, metrics ramp, `gUiDarkTheme`, HC fallbacks); single write site `WindowShell::ApplyTheme` (`:670-709`); F9 light/dark, `WM_SETTINGCHANGE` follow-system, persisted in settings.json; 150 ms cross-fade gated on `SystemAnimationsEnabled()`; `IconCache` = 1 worker, 512-entry LRU, DPI-scaled, `WM_APP_ICON_READY`, no negative cache (failed keys re-resolve every paint).

---

## 13. File Operations

All operations are driven by **`IFileOperation` on a dedicated STA worker thread** with its own message pump (`FileOperations.cpp:165,211-216`) — no `SHFileOperation`, no raw `DeleteFile`/`MoveFile` in the UI. Progress via `IFileOperationProgressSink` (percent/speed/ETA + slim bar); cancellation via Esc + cooperative sink; conflicts resolved **before** execution via a modal in-app dialog (Replace/Skip/Keep Both/Cancel + apply-to-all); name validation (illegal chars, reserved names, trailing dot/space) before create/rename.

| Op | Status | Mechanism / Evidence |
|---|---|---|
| Open | WORKING | `ShellExecuteW`; folders navigate in-app (`QuickActions.cpp:117`) |
| Open With | WORKING | `ShellExecuteExW` verb `openas` (`QuickActions.cpp:121-129`) |
| Copy/Cut/Paste | WORKING (in-app only) | internal `clipboardPaths_` → IFileOperation Copy/Move (`WindowShell.cpp:150-160`; `FileOperations.cpp:346-347`). **Never touches the system clipboard** — no Explorer interop |
| Move | WORKING | `IFileOperation::MoveItem` (`FileOperations.cpp:347`) |
| Rename | WORKING | F2 → modal validated prompt; inline edit only post-create |
| Delete | WORKING | `IFileOperation::DeleteItem` + `FOF_ALLOWUNDO` (recycle default), `FOF_NOCONFIRMATION` (conflicts pre-resolved) |
| Permanent delete | WORKING | Shift+Delete → explicit `MB_OKCANCEL|MB_DEFBUTTON2` confirmation **before** enqueue (`WindowShell.cpp:180-183`) |
| Create folder/file | WORKING | `NewItem` + auto inline rename (`FileOperations.cpp:359-361`) |
| Properties | **STUB** | handler only sets the window title and repaints (`WindowShell.cpp:243-246`) — the one genuinely unimplemented feature |
| Drag & drop (in/out) | WORKING | custom `IDataObject` (CF_HDROP), `DoDragDrop`/`RegisterDragDrop` (`OleDragDrop.cpp:211-228`) |
| Multi-select | WORKING | set-based, Ctrl/Shift/Select-all (`SelectionModel.h:8-39`) |
| Conflict dialog | WORKING | modal, pre-execution, apply-to-all (`ConflictDialog.cpp:70-142`) |
| Progress | WORKING | sink → title bar percent/speed/ETA + bar; "Last Operation Errors…" dialog |
| Cancellation | WORKING | Esc → `CancelCurrent`; current op only |
| Undo | WORKING (session-scoped, in-memory) | PIDL-saved recycle-delete restore, rename-back, move-back; copy/permanent-delete not undoable by design; lost on exit |
| Context menu | WORKING (custom) | custom `CreatePopupMenu`/`TrackPopupMenuEx`; **no shell `IContextMenu`** — "Open With" is the only shell dialog |

Safety: recycle-by-default + confirmed permanent delete; no raw destructive calls; conflicts pre-resolved (OS double-prompt suppressed); undo is in-memory only. COM threading is textbook-correct (no COM marshaling across threads; atomic cancellation). **All e2e behaviors are covered by `fffileoperations_e2e_tests`** (real temp-dir ops incl. recycle-restore, permanent delete, cancel mid-copy, keep-both).

---

## 14. UI Architecture

- Shell: single `HWND`, `WindowShell` owns everything: panels, render orchestration, theming, drag-drop target, WM_APP dispatch, command routing.
- Panels: ColumnView, NavigationWorkspace (+ Chrome/Sidebar), SearchPanel, CommandPalette, Preview, StorageAnalysis (+ TreemapView/CategoryEngine), SettingsDialog, FileOperations, ConflictDialog, IconCache. All compiled into one target (`src/ui/CMakeLists.txt`).
- Commands: `CommandSystem` registry (id-indexed vector), fuzzy palette (Ctrl+Shift+P), `shortcuts.json` binding file, context menus built from the registry. Settings→Shortcuts page is **decorative** (edits `settings_->shortcuts`; real bindings come from `shortcuts.json`; `ShortcutMap::Save/Rebind/Remove` never called).
- Dead/weak surfaces: `item.properties` stub; dead menu constants (`kMenuCopy/Cut/Paste/Delete/...`, `WindowShell.cpp:29-34` — leftovers of a never-built menu); empty stub buttons (`RemoveVolumeRule`/`MoveVolumeRuleUp/Down`); `PopulateGeneralPage` empty; `WM_APP_SETTINGS_CHANGED` never sent; `ColumnView::SetRepaintCallback` never called; palette `HandleDrawItem`/`HandleCtlColor` never routed (palette list paints blank, themed edit colors never apply); `TreemapView::Reposition()` is a self-admitted no-op; bookmark mutation APIs (`Add/Rename/Reorder/RemoveBookmark`) have **no callers** (bookmarks display-only); `SearchPanel` keeps its own unbounded sysicon map on the UI thread.
- No app manifest (no `longPathAware`, no comctl32 v6 manifest — comctl32 is linked); DPI handled via `SetProcessDpiAwarenessContext(PMv2)`.

---

## 15. State Management

**Single visible truth:** `ColumnView::columns_` + `focusedColumnIndex_` — what the user sees.

**Follower copy:** `NavigationContext.currentPath` + per-pane history/historyIndex (drives breadcrumbs, sidebar highlight, back/forward buttons) — synced at **8 distinct sites** (`WindowShell.cpp:118,147,351,548,553,570,1362,1505`) with no funnel (C1).

Known conflicts/corruption (all CONFIRMED):
- **C2 — StorageAnalysis corrupts navigation history:** its `navigate_` callback updates workspace only, never ColumnView (`WindowShell.cpp:570-573`); drill paths pollute back/forward until the next keypress re-syncs.
- **C3 — tab restore broken:** column paths/selections/scroll save-restore exists only in the Ctrl+Tab handler (`WindowShell.cpp:287-299`); clicking a tab collapses the hierarchy to one path; restore writes a pane-0 scroll field `Render` never reads; `columnSelections` stores a single column index — item selections never persist.
- **C4 — startup divergence:** workspace seeds `L"C:\\"` (`WindowShell.h:74`) while column 0 is the drive-list pseudo-root (`CurrentPath()==L"Computer"`) — opening search at startup yields an empty scope path.
- **C5 — scroll state triplicated** for pane 0 (ColumnView/WindowShell/scrollAnim); one copy only feeds the broken restore path.
- **C6 — engine status fanned to 5 copies** (`engineActive_` in 5 classes) — one fact, five latches.
- **C8 — generation-blind UI:** the engine's generation counter is dropped (`EngineClient.cpp:149-153`); each new generation triggers up to **4 independent snapshot re-parses** (ColumnView, SearchPanel, StorageAnalysis, TreemapView).

---

## 16. Threading / Async / Performance

**Thread map (CONFIRMED):** Service — per-connection ctrl thread (2 ms poll, never blocks in ReadFile), per-scan and per-journal workers, per-worker write-deadline watchdog (100 ms poll, `CancelSynchronousIo` on >5 s blocked writes), staleness monitor. Engine — privileged lifecycle thread (connect/verify/handshake/heartbeat 20 s/backoff 1→60 s), reader thread per active connection (**dispatches every batch → ingestion → full snapshot republish on this thread**), reconciliation thread (10 min), watcher thread per watched dir, UI-conn thread per UI (**synchronous degraded enumeration per RequestDirectory**), idle manager. UI — single UI thread + EngineClient management/invalidation threads, SearchPanel worker, IconCache worker, FileOperations worker, Preview workers (up to 64 MB reads), `std::async` per file-type detection.

**Lock map:** all `std::mutex` (no SRWLOCK/CRITICAL_SECTION). `IndexPipeline::mutex_` is one coarse mutex over Store+Projection+rebuild+export (`IndexPipeline.h:125`); `Projection` is documented as not internally synchronized — safe only because that mutex is the sole serializer (`Projection.h:44-47`). No lock cycles found by inspection.

**UI-thread blocking:** no `WaitForSingleObject`/`sleep`/`join` in steady state (grep-clean; joins are shutdown-only) — but **`WM_APP_FILE_OPERATION_EVENT` → `ColumnView::OnSnapshotUpdated` → `ReadSnapshot` (full 8 MiB parse into `std::map` with per-entry wstrings) + per-column `std::sort` runs on the UI thread** (`WindowShell.cpp:1336`; `ColumnView.cpp:483-504`). Also per-keystroke search candidate building on the UI thread (§10).

**Performance anti-patterns (documented, not fixed):**
1. Whole-index export + serialize + republish per ingestion batch (≈O(N²) during initial scan) — `VolumeSessionManager.cpp:358-481`.
2. Full re-enumeration per watch event (degraded) — coalesced but not throttled (`DirectoryWatcher.cpp:47-50`).
3. Per-keystroke full snapshot re-parse on the UI thread, not cancellable (`SearchPanel.cpp:738-776`).
4. StorageAnalysis sort on UI thread (`StorageAnalysis.cpp:536-543`).
5. Unbounded: search results; `UiServer::directories_` map (per browsed folder, never evicted); watched roots; SearchPanel icon map.
6. **Snapshot capacity cliff:** one directory ≳81 k entries fails the entire publish (≈73 B fixed + name per entry, 8 MiB slot) — `SnapshotFormat.h:21`.
7. Blocking pipe broadcast to all UIs under `clientsMutex_` — a non-reading UI stalls the engine reader thread (`UiServer.cpp:109-117`).
8. WAL checkpoint under the pipeline mutex (`IndexPipeline.cpp:78-85`).

**Concurrency risks:**
- **CONFIRMED torn-snapshot-read design gap:** the reader grabs `activeSlot`/`activeSlotDataSize` once, then parses; the publisher can flip the other slot active and overwrite the slot still being parsed (`SnapshotPublisher.cpp:66-82` vs `EngineClient.cpp:86-92` — no generation re-check). x86 ordering covers the flip, not reader lifetime. Frequent per-batch republishes widen the window.
- Coarse serialization: ingestion, exports, and UI aggregate requests all queue behind the pipeline mutex; a slow export starves ingestion.
- Engine-side auth asymmetry: the service re-verifies peer identity on **every heartbeat** (`ServiceConnection.cpp:367-379`), but the engine does **not** re-verify the service after handshake — contradicting the "periodic re-validation both directions" design claim (POSSIBLE gap).

**Memory architecture:** `ProjectionEntry` ≈ 80 B (`Projection.h:24-34`); FlatHashMap ≈ 41 B/entry; children ≈ 4 B/child + ~84 B/dir; NamePool 2 B/char interned. **Total ≈ 150–180 B/entry → 5M entries ≈ 0.8–0.9 GB; 20M ≈ 3+ GB** (bench: ~198 B/entry @ 5M). Snapshot 16 MiB total (~81 k entries/slot); each parse materializes ~20–40 MB transient. Startup `RebuildAll` streams all rows + interns names — O(N), likely tens of seconds at 5M entries; `PRAGMA integrity_check` on every open grows with DB size.

---

## 17. Windows Integration

- **NTFS MFT/USN (service-only):** `DeviceIoControl` with `FSCTL_GET_NTFS_VOLUME_DATA`, `FSCTL_ENUM_USN_DATA`, `FSCTL_GET_NTFS_FILE_RECORD`, `FSCTL_QUERY_USN_JOURNAL`, `FSCTL_CREATE_USN_JOURNAL`, `FSCTL_READ_USN_JOURNAL` (`VolumeScanner.cpp:132,171,208`; `UsnJournalReader.cpp:68-184`). Raw volume opens `CreateFileW(\\.\X:, FILE_FLAG_BACKUP_SEMANTICS)` (`VolumeScanner.cpp:118`). Engine/UI have **zero** `DeviceIoControl` calls — the split holds.
- **Shell/COM:** `IFileOperation` (file ops), `SHGetFileInfoW`/`SHGetStockIconInfo` (icons), `ShellExecute(Ex)W` (open/open-with/terminal), Task Scheduler COM, WIC (thumbnails/preview), OLE drag-drop.
- **Auth:** image-path verification with canonicalized exact-dir match (`PeerImageMatch.cpp:10-37`), Authenticode `WinVerifyTrust` with pinned thumbprints, live `NetLocalGroupGetMembers` group checks (`GroupSetup.cpp:58-88`).
- **Privileges:** service = LocalSystem, delayed auto-start; `FastFilesUsers` group gets **`SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG` only** (never start/stop); install-dir ACL admin-write/client-read+execute; per-user LUA scheduled task launches the engine at logon; startup probes (backup privilege, token diagnostic) are log-only; missing group/ACL → service refuses to start.
- **Long paths:** no `\\?\` prefixing, no `longPathAware` manifest — >260-char paths fail (LIKELY). Reparse points are listed but never followed. Network shares/removable drives: drive detection is `GetLogicalDrives`+`GetDriveTypeW` (fixed drives only in column 0; `StorageAnalysis` has its own copy).

---

## 18. OpenSpec Inventory

`openspec/changes/` — 15 changes (14 closed-by-checkbox + 1 open; `archive/` is empty; closed changes are never archived):

| Change | Tasks | Status |
|---|---|---|
| establish-architecture-foundation | 54/54 | CLOSED — D1..D6 (native C++, 3-process, pipes-only, security model, degraded mode, versioning); D4 superseded by constrained-broker |
| resolve-raw-volume-privilege-insufficiency | 16/16 | CLOSED — evidence matrix; constrained LocalSystem broker decision |
| diagnose-backup-privilege-sufficiency | 14/14 | CLOSED — real-call-site instrumentation; enabled-but-denied 0x5 → genuine insufficiency |
| index-storage-and-scanning | 51/51 | CLOSED — SQLite WAL, FRN-keyed projection, rebuild+incremental, reconciliation, resumable cursor |
| instant-search | 58/58 | CLOSED — snapshot mmap search, tiered ranking, 4 scopes, virtualized list |
| navigation-and-workspace | 48/48 | CLOSED — tabs, dual-pane, address bar, bookmarks, JSON persistence |
| file-operations | 58/58 | CLOSED — IFileOperation STA, conflict dialog, recycle default, undo, OLE DnD |
| file-preview-and-properties | 41/41 | CLOSED — WIC/text preview, properties panel, status bar |
| storage-analysis | 38/38 | **CLOSED by checkbox, but implementation drift (§11): treemap stub, tasks 4.3/4.5/7.3/7.4 overclaim** |
| modernize-ui-appearance | 33/33 | CLOSED — token system, icon cache, gated animation, device-loss recovery |
| settings-and-appearance | 48/48 | CLOSED — JSON settings, index health, diagnostics; stale §2 text |
| shell-integration-and-commands | 44/44 | CLOSED — command registry, palette, context menus, shortcuts |
| autonomous-runtime-verification | 66/68 | OPEN by checkbox (2 deferred `REQUIRED-BUT-UNAVAILABLE`) |
| zero-touch-autonomous-engineering | 46/49 | OPEN by checkbox (3 deferred) |
| **close-independent-validation-gaps** | **2/34** | **OPEN — the actionable open work** (32 tasks: %-of-parent, details-pane layout, docs, pin provisioning) |

`openspec/specs/` — **28** top-level capability specs (requirements-only; zero implementation-status claims), canonical merged copies of the changes' delta specs. Verified: 26/28 are verbatim merges; **2 genuine content drifts**: top-level `storage-drilldown-and-treemap` omits the %-of-parent/overflow-safe-sort refinements, and top-level `index-engine` omits subtree-gating requirements (both predate the 2026-08-06 sync). Explicitly superseded: `privileged-index-service` spec markers (privilege-minimization → constrained broker). Doc inaccuracies: AGENTS.md "CIVG all tasks open" (2 are `[x]`); "27 specs" (actual 28); settings-and-appearance §2 stale blockquote.

---

## 19. OpenSpec Traceability Matrix

| OpenSpec Change / Spec | Intended behavior | Relevant code | Status | Evidence | Drift |
|---|---|---|---|---|---|
| establish-architecture-foundation / column-view-browsing | 3-process split; Finder columns | all of src/ | COMPLETE (foundation) | CMake targets; ColumnView.cpp | D4 superseded by constrained broker — documented |
| privileged-index-service / index-engine | Privileged service; MFT/USN index | VolumeScanner.cpp, UsnJournalReader.cpp, MftParser.cpp, IndexPipeline.cpp | **BROKEN (inert)** | PinnedSignatures.h:16-17 | Whole pipeline dormant until pins provisioned; spec's "stub requirement" properly replaced |
| filesystem-index-store | SQLite store + projection | Store.cpp, Projection.cpp, NamePool.cpp | COMPLETE | tests ffindexstore_* | — |
| instant-search / search-query-engine / search-ui | Keystroke search over index, zero IPC | SearchPanel.cpp, Search.cpp, Query.cpp | PARTIAL | SearchPanel.cpp:738-776; UiProtocol.h (no search msg) | Searches snapshot of browsed dirs, not the index; scopes overstate; `defaultSearchScope` dead |
| storage-overview | Volume overview + sizes | StorageAnalysis.cpp | PARTIAL | RefreshOverview (OS APIs) | Aggregates need index; degraded impossible (volumeRowId=0) |
| storage-drilldown-and-treemap | Drill-down + squarified treemap with dir sizes | TreemapView.cpp, StorageAnalysis.cpp | **DISCONNECTED/STUB** | TreemapView.cpp:106-107; WindowShell.cpp:1147-1149 | No aggregate plumbing to treemap; list views never get WM_NOTIFY; tasks.md overclaims (4.3/4.5/7.x) |
| navigation-and-workspace (tabs/address-bar/sidebar/bookmarks/dual-pane) | Workspace + history + persistence | NavigationWorkspace.cpp, NavigationChrome.cpp | PARTIAL | WindowShell.cpp:287-299,570-573 | Tab restore broken; drill corrupts history; bookmarks display-only; `SetDualPane` never called |
| file-operations-core / conflict-resolution / multi-selection-and-dragdrop | IFileOperation ops + conflicts + DnD | FileOperations.cpp, ConflictDialog.cpp, OleDragDrop.cpp | COMPLETE | fffileoperations_e2e_tests | Properties (file-preview-and-properties) is a stub |
| file-preview-and-properties / properties-and-details / status-bar | Preview + properties + status bar | Preview.cpp | PARTIAL | WindowShell.cpp:243-246 | Preview real; Properties = title-bar stub; status bar thin |
| modernize-ui-appearance / theming / ui-appearance | Tokens, dark mode, icons, animation | UITheme.h, UiStyle.h, UiAnimation.h, IconCache.cpp | COMPLETE | ffui_style_tests | — |
| settings-and-appearance / settings-ui / index-health-and-diagnostics | Settings UI, health, diagnostics | SettingsDialog.cpp, IndexHealth.h, Diagnostics.cpp | PARTIAL | CommandSystem.cpp:268-288; SettingsDialog.cpp:340-341,678-688 | Shortcuts page decorative; several settings dead; empty stub buttons |
| shell-integration-and-commands / command-palette / context-menus-and-quick-actions / keyboard-shortcuts | Command registry, palette, menus | CommandSystem.cpp, CommandPalette.cpp | PARTIAL | WindowShell.cpp:323-327,649 | `toggle-column-view` dup of `toggle-dual-pane`; Ctrl+A unreachable; no shell IContextMenu (by design) |
| resolve-raw-volume-privilege-insufficiency | Constrained broker + evidence | ServiceRegistration.cpp, PrivilegeVerification.cpp | COMPLETE (decision) | evidence/matrix-execution-and-selection.md | Pins still placeholder — operationalization pending |
| diagnose-backup-privilege-sufficiency | Prove insufficiency | PrivilegeVerification.cpp | COMPLETE | evidence/real-service-outcome.md | — |
| close-independent-validation-gaps (current open) | %-of-parent, pane layout, docs, pin hardening | — (not implemented) | OPEN (2/34) | tasks.md | Not started; spec deltas not merged to top-level |
| autonomous-runtime-verification / zero-touch-autonomous-engineering | verify harness, intake loop | verify/*.ps1 | COMPLETE-ish (5 tasks deferred) | verify/ | Deferred external items |
| file-type-categorization | Category engine | CategoryEngine.cpp | COMPLETE | tests | Settings-driven categories dead (B7) |

---

## 20. Feature Status Matrix

| Feature | UI | Logic | Integration | Runtime status | Severity | Notes |
|---|:-:|:-:|:-:|---|---|---|
| Application startup | ✓ | ✓ | ✓ | window appears, responds | P1 | silent exit paths; engine spawn fails from build tree |
| Main shell / chrome / theme | ✓ | ✓ | ✓ | working | P3 | no manifest; repaint on every mousemove |
| Filesystem browsing (degraded) | ✓ | ✓ | ✓ | working when engine reachable | P1 | engine unreachable in build tree (observed) |
| Directory navigation (columns) | ✓ | ✓ | ✓ | working | P2 | state sync quirks (C1) |
| Breadcrumbs / address bar | ✓ | ✓ | △ | working | P3 | follower state, 8 sync sites |
| Back/forward | ✓ | ✓ | △ | working | P2 | corrupted by storage drill (C2) |
| File opening | ✓ | ✓ | ✓ | working | P3 | ShellExecute |
| Search | ✓ | ✓ | △ | partial (browsed dirs only) | P1 | UI-thread parse; snapshot-only; scopes aspirational |
| Indexing (MFT/USN) | — | ✓ | ✗ | inert (pins) | P0 | never exercised end-to-end |
| Live filesystem updates | — | ✓ | △ | partial (watched roots) | P2 | unbounded watchers; overflow kills watch silently |
| Storage analyzer overview | ✓ | ✓ | ✓ | working (OS capacity) | P2 | menu-less (palette only) |
| Drill-down / largest lists | ✓ | ✓ | ✗ | **lists render empty** | P1 | WM_NOTIFY routing bug (B3) |
| Treemap | ✓ | ✓ | ✗ | stub (dirs = 0 B) | P1 | no aggregate wiring; degraded impossible |
| Column view | ✓ | ✓ | ✓ | working | P2 | dual-pane toggle dead; Ctrl+A unreachable |
| Copy/Move/Rename/Delete/Create | ✓ | ✓ | ✓ | implemented + e2e-tested | P2 | internal clipboard only (no Explorer interop) |
| Recycle bin | ✓ | ✓ | ✓ | default, undoable in-session | P2 | in-memory undo only |
| Drag & drop | ✓ | ✓ | ✓ | implemented | P3 | — |
| Tabs | ✓ | ✓ | △ | partial | P2 | restore broken (C3) |
| Context menus | ✓ | ✓ | ✓ | custom popup | P3 | no shell verbs |
| Thumbnails / icons | ✓ | ✓ | ✓ | working | P3 | no negative cache |
| Sorting / filtering | ✓ | ✓ | △ | partial | P3 | UI-thread sorts; no column sort UI |
| Settings | ✓ | ✓ | △ | partial | P3 | decorative pages, dead settings |
| Dark/light theme | ✓ | ✓ | ✓ | working | P3 | — |
| Keyboard navigation | ✓ | ✓ | △ | partial | P3 | chord dispatch conflicts; no Backspace |

---

## 21. Disconnected Implementations

1. **Privileged NTFS pipeline ↔ live service** — fully implemented (scanner, journal reader, parser, batch streaming, ingestion) but unreachable: placeholder pins. Backend: EXISTS; wiring: MISSING (auth); result: dormant by design.
2. **Treemap ↔ folder aggregates** — `EngineClient::RequestFolderAggregate` exists and is used by the drill-down list, but **never by TreemapView**. Data source exists one method away; the two were never connected.
3. **StorageAnalysis lists ↔ WindowShell WM_NOTIFY** — list views created as `LVS_OWNERDATA` but notifications are routed only to SearchPanel; `HandleNotify` never called. UI exists; data feed: MISSING.
4. **Storage analysis ↔ user** — reachable only via Ctrl+Shift+P (no menu, no shortcut).
5. **Dual pane ↔ command** — `SetDualPane` never called; `toggle-column-view` is a copy of `toggle-dual-pane`.
6. **Bookmarks ↔ mutation UI** — display-only; add/rename/reorder/remove APIs have no callers.
7. **Settings ↔ behavior** — `defaultSearchScope`, `startupLocation`, `restorePreviousSession`, `storageCategories` written, never read; shortcuts page edits a dead map; `CategoryEngine` settings API zero callers.
8. **`Store::GetFolderAggregate` (SQLite CTE) ↔ runtime** — only unit tests call it; wire uses the Projection walk.
9. **Search ↔ index** — no protocol path from search to the SQLite index by design; in practice search only sees the snapshot.
10. **Status bar / properties** — status bar thin; properties is a title-bar stub.

---

## 22. Duplicate / Legacy Implementations

**Surprisingly clean.** Grep for `V2|Old|Legacy|Stub|Mock|Dummy|Unused|Deprecated|Obsolete` over `src/`: zero real hits (only benign false positives). Single implementations of: MFT parser, ingestion pipeline, snapshot format, search entry, directory providers (degraded enumerator vs. privileged scanner are complementary layers, not duplicates). Single `ffui::FormatSize` (`Util.cpp:10`).

The one historical duplication was resolved at merge time: **PRs #3 and #4 (`quantdale/claude/index-storage-scanning-spec-*`) were parallel, divergent implementations of the same OpenSpec change**; the merge kept PR #3's generation and deleted PR #4's `MftRecordParser.cpp`, `NtfsRawAccess.cpp`, `IngestionPipeline.cpp`, `DurableStore.h`, `IndexStore.h`, etc. The losing generation survives only in git history (both lineages are ancestors of main). ~21 orphaned `refs/cline/checkpoints/*` snapshots are abandoned agent work (superseded; safe deletion candidates, not deleted here).

Minor duplication: volume probing re-implemented at 6+ sites (`GetLogicalDrives` in Settings.cpp, VolumeEnumeration.cpp, VolumeIdentity.cpp, ColumnView.cpp, SettingsDialog.cpp, StorageAnalysis.cpp, NavigationWorkspace.cpp) with no shared helper.

---

## 23. Critical Bugs

**P0 — App unusable:**
- (P0) Privileged path inert → no index → headline features (full-disk search, drive-wide analysis) have no substrate. `PinnedSignatures.h:16-17`. *Deliberate fail-closed, but it is the single biggest reason the product can't do what it promises.*
- (P0/P1) **Engine not reachable from the UI in the build-tree layout** — same-dir launch assumption (`EngineClient.cpp:38-40`); observed: window open, no engine process, hence no directory data beyond drive roots. (Installed layout likely fine — unverified.)

**P1 — Core feature broken:**
- Search covers only browsed directories; scope labels ("Current Drive", "All Indexed Locations") overstate; 8 MiB snapshot cap makes whole-volume export fail silently (`SnapshotPublisher.cpp:62-64`).
- StorageAnalysis list views render empty (WM_NOTIFY routing, `WindowShell.cpp:1147-1149`); treemap stub (`TreemapView.cpp:106-107`); degraded aggregates structurally impossible (`StorageAnalysis.cpp:458`).
- Silent failure fabric: startup exits with no dialog (`Main.cpp:25-33`); engine relaunch loop invisible; oversize publish silent; watcher overflow kills watch silently; no staleness detection (`EngineClient.cpp:83-85`).
- Snapshot torn-read design gap (publisher can overwrite the slot being parsed) — `SnapshotPublisher.cpp:66-82` vs `EngineClient.cpp:86-92`.

**P2 — Major defects:**
- Storage drill corrupts navigation history (C2); tab restore broken (C3); startup path divergence (C4); scroll state triplicated (C5); engine status in 5 copies (C6); generation-blind 4× re-parse (C8).
- Dual-pane and column-view toggles inert (`SetDualPane` no callers; `WindowShell.cpp:323-327`).
- Unbounded DirectoryWatcher root map; unbounded `UiServer::directories_`; unbounded search results.
- UI-thread snapshot parse on file-op events; per-keystroke UI-thread candidate build.
- Ctrl+A unreachable; edit-box Ctrl+C/V/Delete hijacked by global chords.
- Engine-side auth re-validation asymmetry (service re-verifies per heartbeat; engine never re-verifies service identity).
- Whole-index export per ingestion batch (O(N²) scan); blocking pipe broadcast.

**P3 — Minor:** Properties stub; dead menu constants; decorative settings pages/stub buttons; bookmarks display-only; palette styling never applied (B5); icon no-negative-cache; no `VK_BACK`; no manifest/longPathAware; hardcoded `C:\` seed (`WindowShell.h:74`); `dist/` stale; console windows (engine/installer console subsystem).

**P4 — Debt:** torn-read + coarse-mutex architecture (needs design fix, not bug fix); doc drift (SnapshotFormat.h comment, DegradedModeEnumerator accessible contract, AGENTS.md task claims, 27-vs-28 specs, storage tasks.md overclaims); log rotation absent; `SearchPanel` unbounded icon map; flaky tests #20/#23 under `-j4`.

---

## 24. Root Causes of Current Unusability

1. **RC1 — The index can never exist (pins).** Every feature that differentiates the product depends on data that the shipped configuration cannot produce. Not a bug — a deliberate fail-closed design awaiting signing — but it makes the current build a degraded-mode-only file manager that doesn't say so.
2. **RC2 — Search and storage analysis are wired to the snapshot, not the index, and the snapshot is per-directory-sized.** The architecture conflates "instant search substrate" (needs whole-tree) with "directory listing channel" (per-dir). Result: search silently degrades to browsed folders, and a full-volume export exceeds the 8 MiB slot and fails without error.
3. **RC3 — The UI never tells the user what's missing.** Silent exit-1 startup, silent engine-retry loop, silent publish failures, silent watcher death, silent stale data. A user staring at an empty column cannot distinguish "loading", "failed", "degraded", or "not connected".
4. **RC4 — UI state has no single owner.** ColumnView, NavigationWorkspace, WindowShell, StorageAnalysis, and SearchPanel each hold fragments of path/selection/history state synced by hand at 8+ sites; every new surface corrupts another's state (history pollution, broken tab restore).
5. **RC5 — Engine reachability is layout-fragile.** Same-dir exe discovery works for the installed layout only; dev/build-tree runs silently degrade to an empty shell.

---

## 25. Build/Test Health

- Build: **PASS** (incremental, zero warnings, `/W4 /WX` enforced; full suite ~13 s).
- Tests: **29/29 in serial; 2 flaky under `-j4`** — `fffileoperations_tests` #20 (conflict-dialog timing) and `ffpreview_tests` #23 (supersede/rapid-burst deadlines); both pass in isolation; root cause is test-side timing races (REPOSITORY).
- CI: `.github/workflows/ci.yml` builds debug/release/analyze on windows-latest and runs ctest (skipped for analyze).
- Coverage gaps: DirectoryWatcher, VolumeScanner, UsnJournalReader, SnapshotPublisher, ServiceConnection, ColumnView, Renderer, WindowShell have zero test references; no snapshot parse/serialize round-trip unit test; no live pipe-auth E2E; bench not registered (by design).
- `ffdoc_drift_tests` (pwsh) guards guidance docs against superseded claims and mixed line endings.

---

## 26. Performance Assessment

The design intent (zero-IPC keystroke search over a mapped snapshot; persistent SQLite index; USN delta updates) is sound, but the current implementation has four architecture-level performance contradictions:

1. **Search cost is dominated by snapshot re-parsing, not matching** — and it happens on the UI thread, per keystroke, uncancelled.
2. **Per-batch whole-volume export** turns an incremental ingestion into O(index) work per batch — the exact opposite of "incremental".
3. **The 8 MiB snapshot slot is a hard ceiling** that a real volume's serialized tree will exceed — the feature fails exactly when it should shine.
4. **Degraded mode rescans whole directories per change event** with no throttling, and watchers accumulate forever.

Memory: projection at ~150–180 B/entry is defensible (≈0.9 GB @ 5M entries); the transient snapshot parse (~20–40 MB per parse × concurrent consumers) and unbounded UI-side caches are the practical risks.

---

## 27. Technical Debt (meaningful only)

- Snapshot reader/publisher lifetime contract undocumented + violated (torn reads) — needs design fix.
- Coarse `IndexPipeline::mutex_` serializing ingestion+export+aggregates.
- Hand-synced UI state (RC4) — the single most expensive debt class.
- Doc drift in 6 places (specs, AGENTS.md, SnapshotFormat comment, tasks.md overclaims).
- Test flakiness (#20/#23); no E2E for the live pipe auth or renderer.
- No manifest, no long-path support, console-subsystem exes, no log rotation, stale `dist/`.

---

## 28. Reuse vs Rebuild Matrix

| Subsystem | Verdict | Basis |
|---|---|---|
| Protocol + IPC (ffprotocol/ffipc) | **KEEP AS-IS** | hardened, fuzzed, tested, clean |
| Service (indexsvc) | **KEEP AS-IS** | real scanner/journal/parser; only pins missing |
| Engine (IndexPipeline/Projection/Store) | **KEEP + FIX** | solid; fix per-batch export, watcher bounds, torn-read |
| Snapshot publish/read | **KEEP + FIX** | fix capacity handling + generation re-check |
| Search (ffsearch + SearchPanel) | **KEEP + FIX** | offload candidate build off UI thread; honest scopes |
| Storage analyzer | **KEEP + FIX** | wire WM_NOTIFY, wire treemap aggregates, add menu entry |
| Treemap layout math | **KEEP AS-IS** | tested; only data feeding is broken |
| Column view | **KEEP + FIX** | fix toggles, chord routing, state ownership |
| NavigationWorkspace | **KEEP + REFACTOR LATER** | state funnel; tab restore |
| File operations | **KEEP AS-IS** | e2e-tested; add system-clipboard interop later |
| Settings | **KEEP + FIX** | wire dead settings; remove decorative pages |
| UI shell/theme/icons/renderer | **KEEP AS-IS** | modern, tested, device-loss handled |
| Installer/verify harness | **KEEP AS-IS** | — |
| Nothing in the tree needs full replacement | — | — |

---

## 29. Recommended Recovery Sequence

*Recommendation only — not implemented.*

- **Milestone 0 — Make launch honest and reliable.** Startup error dialog; engine discovery in build layout (look in own dir, then sibling `engine/` dir, then PATH/install dir); surface engine-unreachable in the UI; kill the silent 2 s relaunch loop after N attempts. *Slices: EngineClient, Main.cpp (UI).*
- **Milestone 1 — Basic file browsing (degraded-first).** Verify end-to-end browsing in both layouts; add enumeration cancellation + watcher bounds + overflow recovery; address-bar/column state funnel (start of RC4). *Slices: UiServer, DirectoryWatcher, DegradedModeEnumerator, WindowShell.*
- **Milestone 2 — Honest, faster search.** Off-UI-thread candidate build; cap results; scope labels reflect reality (or make "Current Drive" do a bounded degraded walk); fix snapshot capacity behavior (publish partial + error, or per-directory slots). *Slices: SearchPanel, SnapshotFormat/Publisher, UiServer.*
- **Milestone 3 — Storage analyzer vertical.** WM_NOTIFY routing; treemap↔aggregate wiring; menu entry; degraded-mode sizes via a lazy size-walk with caching. *Slices: WindowShell, StorageAnalysis, TreemapView, engine.*
- **Milestone 4 — State consolidation.** Single navigation context; fix tab save/restore; remove C2/C4/C5/C6 duplication. *Slices: NavigationWorkspace, ColumnView, WindowShell.*
- **Milestone 5 — Privileged path activation (product decision).** Either (a) provision a signing cert + real pins + rotation (CIVG tasks 5.x), or (b) re-scope: engine-side unprivileged full-tree indexer feeding the existing Store (slower first build, then USN not available unprivileged — needs watcher-based deltas). This is the only real architecture decision in the recovery plan.
- **Milestone 6 — File ops completeness.** System-clipboard interop, properties dialog, shell context menu, undo persistence.
- **Milestone 7 — Performance hardening.** Per-directory snapshot export, generation re-check, watcher throttling, UI-thread audit, coarse-lock splitting, manifest/long-paths, log rotation.

---

## 30. Proposed Walking Skeleton

The smallest genuinely usable FastFiles: **Launch → drive list → browse folders (degraded) → open files → search within browsed folders**, with every failure visible. Components that must participate: `WindowShell` (startup + error surfacing), `EngineClient` (reliable engine spawn), `UiServer` + `DegradedModeEnumerator` + `DirectoryWatcher` (browse), `ColumnView` + `SnapshotPublisher` (render), `SearchPanel` (basic scopes), `FileOperations` (open/copy/rename/delete). All exist and are mostly working; the milestone-0/milestone-1 fixes above are the entire delta. **No rewrite required; this is a stabilization project, not a reconstruction project.**

---

## 31. Top 10 Actions

1. **Priority: P0 — Fix engine discovery + startup honesty.** Problem: UI silently runs without engine in build layout; silent exit-1 paths. Why: everything else depends on a reachable engine. Affected: `src/ui/src/EngineClient.cpp:23-50`, `src/ui/src/Main.cpp:25-33`, `WindowShell`. Outcome: reliable launch in all layouts with visible errors. Depends: nothing. Risk: low.
2. **Priority: P0/P1 — Provision signature pins or re-scope the privileged path.** Problem: whole index pipeline inert. Affected: `PinnedSignatures.h`, signing pipeline, CIVG tasks 5.x. Outcome: product decision made with evidence. Depends: cert authority/signing decision. Risk: high (process/security), must be deliberate.
3. **Priority: P1 — Search substrate honesty + performance.** Off-UI-thread candidate build; realistic scope labels; snapshot capacity failure surfaced. Affected: `SearchPanel.cpp:738-776`, `SnapshotPublisher.cpp:62-64`. Outcome: search is fast for what it claims and truthful about what it covers. Depends: M0/M1. Risk: low.
4. **Priority: P1 — Storage analyzer: WM_NOTIFY routing.** Affected: `WindowShell.cpp:1147-1149`. Outcome: drill-down/largest lists render. Depends: nothing. Risk: very low.
5. **Priority: P1 — Treemap aggregate wiring.** Affected: `TreemapView.cpp:106-107`, `StorageAnalysis`, `EngineClient::RequestFolderAggregate`. Outcome: treemap shows directory hierarchy. Depends: #4; degraded mode needs FRN/volumeRowId in degraded entries. Risk: medium.
6. **Priority: P2 — Navigation state funnel.** One owner for path/selection/history; fix tab restore; remove C2 corruption. Affected: `NavigationWorkspace`, `ColumnView`, `WindowShell` (8 sync sites). Outcome: history/tabs/panes consistent. Depends: M1. Risk: medium.
7. **Priority: P2 — Watcher/enumeration bounds + cancellation.** Affected: `DirectoryWatcher.h:37`, `DirectoryWatcher.cpp:44-46`, `UiServer.cpp:239-269`. Outcome: no unbounded growth; no silent watch death. Depends: M1. Risk: low.
8. **Priority: P2 — Snapshot lifecycle correctness.** Generation re-check on read; documented reader lifetime; capacity behavior. Affected: `EngineClient.cpp:86-92`, `SnapshotPublisher.cpp:66-82`. Outcome: no torn reads, no silent staleness. Depends: M1. Risk: low-medium.
9. **Priority: P3 — File-ops completeness.** System clipboard interop, real Properties dialog, shell verbs. Affected: `FileOperations`, `WindowShell.cpp:243-246`. Outcome: Explorer parity basics. Depends: M1. Risk: low.
10. **Priority: P3/P4 — Flaky tests + doc drift.** Harden `fffileoperations_tests` #20 / `ffpreview_tests` #23; re-sync specs; correct AGENTS.md counts; fix SnapshotFormat comment. Outcome: trustworthy gate. Depends: nothing. Risk: very low.

---

## 51. Repository Truth Table

| Claim | True? | Confidence | Evidence |
|---|---|---|---|
| Fast Files currently launches successfully | TRUE (window) / PARTIAL (no engine) | CONFIRMED | runtime §7; `EngineClient.cpp:38-40` |
| Basic folder navigation works | PARTIAL — pipeline real, engine unreachable in build layout | CONFIRMED/LIKELY | §9; runtime observation |
| Search is implemented end-to-end | PARTIAL — snapshot-of-browsed-dirs only | CONFIRMED | §10 |
| Search uses an index | FALSE in practice (DB never populated; search reads snapshot) | CONFIRMED | §10 |
| Search uses NTFS MFT | NO (pins fail closed) | CONFIRMED | `PinnedSignatures.h:16-17` |
| Search reacts to filesystem changes | PARTIAL (watched browsed roots only; overflow kills watches silently) | CONFIRMED | `DirectoryWatcher.cpp:44-46` |
| Storage scanning works | PARTIAL (aggregates only for indexed volumes; none in practice) | CONFIRMED | §11 |
| Treemap rendering works | NO (stub; dirs = 0 B) | CONFIRMED | `TreemapView.cpp:106-107` |
| Column navigation works | PARTIAL (render real; state bugs; dual-pane dead) | CONFIRMED | §12, §15 |
| File operations work | YES (implemented + e2e-tested) | CONFIRMED | `fffileoperations_e2e_tests` |
| OpenSpec matches implementation | NO (storage-analysis overclaims; scopes; dead settings) | CONFIRMED | §18-19 |
| Main architecture is internally consistent | MOSTLY YES — with 5 documented seams | CONFIRMED | §5, §24 |
| The app is suitable for daily use | NO | CONFIRMED | §7, §23 |

---

## 52. Current Execution Graph

```
APP START (UI)
   ├── initialization ──✗ silent exit-1 on any failure (no dialog)
   ├── Renderer (D3D11/D2D/DComp) ──✓
   ├── EngineClient ──△ engine spawn: same-dir lookup fails in build tree → 2 s retry forever
   ├── engine reachable? ──✗ observed NOT reachable (no FastFilesEngine process)
   └── main window ──✓ renders (drive-root column only in practice)

BROWSER
   ├── column 0 drives ──✓ (GetLogicalDrives, fixed only)
   ├── RequestDirectory → UiServer ──△ needs engine up
   ├── DegradedModeEnumerator ──✓ (when engine up)
   ├── snapshot publish ──△ silent failure >8 MiB; torn-read risk
   └── column render ──✓

SEARCH
   ├── debounce ──✓ | candidate build (UI thread) ──△ | ExecuteSearch (worker) ──✓
   └── substrate = snapshot of browsed dirs ──✗ not the index

STORAGE ANALYZER
   ├── overview (OS APIs) ──✓ | aggregates ──△ indexed-only
   ├── list views ──✗ WM_NOTIFY not routed | treemap ──✗ no dir sizes
   └── entry ──△ palette-only (Ctrl+Shift+P)

COLUMN VIEW
   ├── model/drill/selection ──✓ | dual-pane toggle ──✗ dead
   ├── history/back-forward ──△ workspace follower, 8 sync sites
   └── tab restore ──✗ broken

FILE OPS (IFileOperation) ──✓ implemented, e2e-tested
PRIVILEGED PATH (service) ──✗ inert: pins placeholder → handshake always rejected
```

---

## 53. What Should We NOT Touch Yet?

- `ffprotocol` / `ffipc` — hardened, fuzzed, security-critical; change only with the security model.
- `VolumeScanner` / `UsnJournalReader` / `MftParser` — real NTFS code never yet exercised live; touch only after the pins question is resolved.
- `IndexPipeline` store/projection semantics ("commit-to-store-before-projection", reconciliation) — correct and tested; only the per-batch export path should change.
- `FileOperations` COM threading + conflict flow — textbook-correct; preserve the model.
- Service/installer security descriptors, ACLs, group logic, TOCTOU defenses — verified correct; do not "simplify".
- `Renderer` device-loss recovery, theming token set, `IconCache` — recent, working, tested.
- `verify/` harness and OpenSpec artifacts — leave until recovery direction is set.

---

## 54. What Is Probably Safe to Remove Later

- Dead menu constants `kMenuCopy/Cut/Paste/Delete/PermanentDelete/NewFolder` (`WindowShell.cpp:29-34`) — leftovers, never referenced (CONFIRMED, low risk).
- Dead code paths: `ColumnView::SetRepaintCallback` (never called), `HandleTimer`/`kRefreshTimer` in StorageAnalysis, `WM_APP_SETTINGS_CHANGED` constant, `TreemapView::Reposition()` no-op, bookmark mutation APIs (until bookmarks get a UI), `Store::GetFolderAggregate` if the projection walk stays canonical, write-only members (`hoverEnterMs_`, `lastAnimTickMs_`, extra `darkTheme_` copies, `columnSelections`/scroll restore fields that Render never reads).
- Orphaned `refs/cline/checkpoints/*` (~21 abandoned agent snapshots) — housekeeping, not source.
- Decorative settings UI (empty `PopulateGeneralPage`, stub volume-rule buttons) once settings wiring is decided.
- No legacy generations exist in `src/` (the PR #4 lineage is already gone from the tree).

---

## 55. Unknown / Needs Verification

- **UNKNOWN: why the engine did not spawn in the runtime test** — CONFIRMED code path (same-dir lookup) + observed absence make the cause LIKELY, but the exact failure (missing exe vs. crash-on-start) was not isolated. Verify by launching `FastFilesEngine.exe` from `build/debug/src/engine/` directly and by testing the installed layout.
- **UNKNOWN: whether the installed layout (all exes in one dir) browses end-to-end** — no elevated/installer run was performed (out of scope). Verify via `verify.ps1 install` in an authorized session.
- **UNKNOWN: behavior of the privileged path** — no live scan has ever run (pins). MFT/USN ingestion, reconciliation, journal resume are code-tested only, never service-tested. Requires signed binaries + pins.
- **UNKNOWN: torn-snapshot-read real-world impact** — design gap confirmed; whether users observe stale columns depends on republish frequency.
- **UNKNOWN: >8 MiB directory behavior in practice** — the cliff is real; which real directories exceed ~81 k entries needs a live test.
- **UNKNOWN: service on this machine** — `FastFilesIndexSvc` process exists but state/auth unverifiable from a non-admin shell.
- **UNKNOWN: UI responsiveness under large directories** — no perf harness beyond the memory bench.
- **POSSIBLE: remote-desktop/VM rendering** — Renderer has no WARP fallback; `CreateSwapChainForComposition` may fail headless (silent exit-1).

---

# FAST FILES — CHATGPT HANDOFF CONTEXT

*Copy this section into ChatGPT to brief it on FastFiles without repository access.*

**What Fast Files is.** FastFiles is a Windows-only, native C++20 file manager (repo: `D:/Documents/tryPython/FastFiles`) aiming to be a modern replacement for File Explorer with WizFile-style instant search, WizTree-style storage analysis, and macOS-Finder-style Column View. It is a long-running side project built almost entirely with coding agents, and it is currently **not usable as a file manager** despite a large, mostly real codebase.

**Architecture (the one decision everything rests on).** Three processes over shared static libraries, joined by two named-pipe seams:
- `FastFilesIndexSvc` (src/indexsvc/) — a LocalSystem Windows service acting as a deliberately **stateless, constrained privileged broker**: it opens raw volumes (`CreateFileW(\\.\X:)` + `DeviceIoControl`) and streams MFT records and USN Journal deltas to the engine. Closed command protocol (`Commands.h`), connection-scoped opaque handles, per-heartbeat re-verification of the client.
- `FastFilesEngine` (src/engine/) — unprivileged, per-logon-session. Owns a SQLite index (`%LOCALAPPDATA%\FastFiles\index.db`, WAL) + in-memory projection, publishes an immutable double-buffered **memory-mapped snapshot** (`Local\FastFiles.IndexSnapshot.<sid>`, 2 × 8 MiB slots) that the UI maps and reads directly, and serves the UI over a control pipe (`\\.\pipe\FastFiles.Ui.Ctrl.<sid>`). Also provides degraded mode: `FindFirstFileExW` enumeration + `ReadDirectoryChangesW` watches per browsed root.
- `FastFiles` (src/ui/) — Win32 GUI, Direct2D/DirectWrite/DirectComposition Column View. Search reads the snapshot **in-process** (zero IPC per keystroke).
- Plus `FastFilesSetup` (installer: service registration, `FastFilesUsers` group, install-dir ACL, scheduled task) and `fftest` (privilege probe). Shared libs: `ffprotocol` (wire), `ffipc` (pipe framing), `ffsetup` (privileged ops), `ffindexstore` (SQLite store + projection + interned name pool + flat hash maps), `ffsearch` (query parsing + in-memory scan).

**Tech stack.** C++20, CMake presets (debug/release/analyze), MSVC `/W4 /WX` (warnings are errors), SQLite 3.53.4 vendored, Direct2D/DWrite/DComp/D3D11, COM (`IFileOperation`, Task Scheduler, WIC), Win32 NTFS APIs, named pipes, Authenticode (`WinVerifyTrust`) mutual auth, PowerShell verification harness. No managed code, no XAML, no third-party UI libs. Tests are plain C++ CTest executables with a hand-rolled `Check()` helper — no gtest.

**Build/test state.** Build is green. 29 CTest tests pass in serial; 2 are flaky under `-j4` (`fffileoperations_tests` conflict-dialog timing; `ffpreview_tests` rapid-burst deadline) — test-side races, both pass in isolation. The app launches to a live, responding window; **but in the observed run no `FastFilesEngine` process appeared**, consistent with the UI launching the engine from its own directory (`EngineClient.cpp:23-50`) while the build tree puts it in a sibling directory — so the observed session could not browse beyond the drive list. In an installed layout all exes share one directory, which likely works but is unverified.

**The central blocker (P0).** The privileged path is **inert by design**: mutual-auth signature pins in `src/setup/include/ffsetup/PinnedSignatures.h:16-17` are all-zero placeholders; `VerifyPinnedSignature` fails closed, so no handshake can ever succeed and **no MFT/USN scan has ever run end-to-end**. The SQLite index is therefore empty in practice. Everything index-dependent — full-disk instant search, drive-wide storage analysis — has no substrate. The rest of the product runs in degraded mode (a first-class, intended state).

**What actually works today.** Degraded browsing pipeline (drive list → per-directory `FindFirstFileEx` enumeration → snapshot → column render); file operations via `IFileOperation` on a dedicated STA thread with conflict dialog, progress, cancellation, recycle-bin default, and session-scoped undo (e2e-tested); OLE drag-and-drop; preview pane; command palette; settings dialog; theming (design tokens, dark/light, gated animation); icon cache; device-loss recovery. The protocol/IPC/security layers are hardened, fuzzed, and unit-tested.

**What is broken or disconnected.**
1. **Search** (`SearchPanel` + `ffsearch`) scans only the snapshot — i.e., **directories the user has browsed**. Scope labels "Current Drive / All Indexed Locations" are aspirational; there is no search message on the UI↔engine protocol by design. Candidate building (full snapshot re-parse into a vector) happens on the **UI thread per keystroke**; results are unbounded; the 8 MiB snapshot slot can silently refuse a publish (no error, stale data). `defaultSearchScope` setting is dead.
2. **Storage analyzer**: volume overview works (OS APIs); drill-down aggregates exist (Projection walk via `RequestFolderAggregate`) but only for indexed volumes, and the **list views render empty** because `WindowShell` routes `WM_NOTIFY` only to SearchPanel (`WindowShell.cpp:1147-1149`) — `LVS_OWNERDATA` with no `LVN_GETDISPINFOW`. The **treemap is a stub**: directories get size 0 (`TreemapView.cpp:106-107`) and it never requests aggregates. Degraded mode cannot compute subtree sizes at all (degraded entries have no FRN/volumeRowId, and `StorageAnalysis.cpp:458` skips `volumeRowId==0`). Reachable only via Ctrl+Shift+P (no menu item). Its `tasks.md` is marked 38/38 `[x]` — a spec-vs-code overclaim.
3. **State fragmentation**: current-path/selection/history is hand-synced at 8+ sites between ColumnView, NavigationWorkspace, and WindowShell. Storage drill-down corrupts back/forward history; tab restore is broken; dual-pane machinery exists but `SetDualPane` is never called (the `toggle-column-view` command is a verbatim copy of `toggle-dual-pane`); Ctrl+A is unreachable; Ctrl+C/V/Delete are hijacked while typing in edit boxes; no Backspace-to-parent; startup seeds `C:\` while column 0 is a drive-list pseudo-root.
4. **Silent failure fabric**: startup failures exit code 1 with no dialog; missing engine → invisible 2 s relaunch loop forever; snapshot publish overflow → silent; watcher buffer overflow kills the watch silently; no staleness detection; no log rotation; engine/installer are console-subsystem (stderr invisible when GUI-launched).
5. **Performance architecture**: whole-volume snapshot export + republish **per ingestion batch** (≈O(N²) during initial scans); per-change full directory re-enumeration; blocking pipe broadcast; torn-snapshot-read risk (reader doesn't re-check generation while parsing — publisher may overwrite the slot being read); one coarse mutex over store+projection+export.
6. **Minor**: Properties is a title-bar stub; bookmarks display-only; several settings written but never read; decorative settings pages; no app manifest (no long-path awareness); stale `dist/FastFiles.exe`; dead menu constants; ~21 abandoned git checkpoint refs (safe to delete); two flaky tests.

**OpenSpec.** 15 changes under `openspec/changes/` (12 fully closed by checkbox; `close-independent-validation-gaps` is the actionable open change with 32 open tasks including pin provisioning; `autonomous-runtime-verification` and `zero-touch-autonomous-engineering` have 5 deferred tasks). 28 top-level specs in `openspec/specs/` are canonical requirement documents. Known drift: storage-analysis tasks overclaim treemap; top-level `storage-drilldown-and-treemap` and `index-engine` specs are missing later refinements; AGENTS.md task counts are stale.

**Security posture (verified sound).** Closed command protocol with no generic path/handle primitive; frame hardening (1 MiB max checked before allocation, batch count/bytes cross-validation, reject-whole-record on bad length fields); pipes ACL'd (SYSTEM+Admins / client group; current user); `FILE_FLAG_FIRST_PIPE_INSTANCE` anti-squatting; service refuses to start without its group; client group has query-only SCM rights; MFT parser never forwards `$DATA`; DLL search-path hardening; service self-heals via binary-hash staleness + SCM failure actions; installer uses scratch-dir TOCTOU defense. One gap: engine-side identity re-validation is one-directional (service re-verifies per heartbeat; engine doesn't re-verify the service after handshake). No secrets found in the repo.

**Root causes of unusability (in one paragraph).** The index can never exist (placeholder pins), so the product's differentiators have no data; search and storage analysis are wired to a snapshot that only contains browsed directories and can silently refuse a publish; the UI never reports any of this (silent exits, invisible retry loops, silent watch death, no staleness signals); and UI state has no single owner, so navigation history/tabs/panes drift. On top of that, the engine isn't even reachable from a build-tree run.

**Recovery recommendation.** This is a stabilization project, not a reconstruction: Milestone 0 — honest startup + reliable engine discovery (look in own dir, sibling dir, install dir; visible errors); Milestone 1 — degraded browsing hardening (watcher bounds, overflow recovery, enumeration cancellation, state funnel); Milestone 2 — honest/fast search (off-UI-thread candidate build, result caps, realistic scopes, snapshot capacity handling); Milestone 3 — storage analyzer vertical (WM_NOTIFY routing, treemap↔aggregate wiring, menu entry, degraded size-walk); Milestone 4 — state consolidation (single navigation context, tab restore); Milestone 5 — **the one architecture decision**: provision real signature pins + signed binaries, or re-scope to an unprivileged full-tree indexer feeding the existing Store; Milestone 6-7 — file-ops parity (system clipboard, real Properties, shell verbs) and performance hardening (per-directory export, generation re-check, lock splitting, manifest/long paths).

**Top files to know.** `src/ui/src/EngineClient.cpp` (engine spawn + snapshot mapping), `src/ui/src/WindowShell.cpp` (composition, WM_NOTIFY routing bug, state sync sites), `src/ui/src/ColumnView.cpp`, `src/ui/src/SearchPanel.cpp`, `src/ui/src/StorageAnalysis.cpp` + `TreemapView.cpp`, `src/engine/src/UiServer.cpp` + `DegradedModeEnumerator.cpp` + `DirectoryWatcher.cpp` + `IndexPipeline.cpp` + `VolumeSessionManager.cpp`, `src/indexsvc/src/ServiceConnection.cpp` + `VolumeScanner.cpp` + `UsnJournalReader.cpp`, `src/protocol/include/ffprotocol/{UiProtocol.h,SnapshotFormat.h,Commands.h}`, `src/setup/include/ffsetup/PinnedSignatures.h`, `src/indexstore/include/ffindexstore/Projection.h`, `tests/` (per-component).

**Unanswered questions.** Why exactly the engine didn't spawn in the observed run (likely same-dir lookup, unverified); whether the installed layout works end-to-end (no installer run was performed); behavior of the privileged path under real signatures (never exercised); real-world impact of the torn-snapshot-read and >8 MiB directory cliff; whether to invest in pins+signing vs. an unprivileged indexer (the pivotal product decision).
