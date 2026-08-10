# Tasks — Recover Degraded-Mode Browsing

Implementation tasks for the degraded-mode browsing recovery. Behavior contracts live in `specs/degraded-browsing-recovery/spec.md`; decisions D1–D6 live in `design.md`. A task may only be marked `[x]` after its runtime acceptance condition succeeds — code that merely compiles does not close a task.

## 1. Engine discovery (design D1)

- [x] 1.1 Add `src/ui/src/EngineDiscovery.h/.cpp`: ordered candidates (same dir, sibling `engine` dir via `GetFullPathNameW`, `%ProgramFiles%\FastFiles`), case-insensitive dedupe, regular-file validation. — complete; focused `ffengine_discovery_tests` and candidate-order/runtime evidence are in `evidence/engine-discovery.md`.
- [x] 1.2 Wire discovery into `EngineClient::LaunchEngineIfNotRunning`; launch only validated candidates. — complete; the Debug UI reached `build\debug\src\engine\FastFilesEngine.exe` in the disposable development-tree launch.
- [x] 1.3 Log the resolved path, launch successes, launch failures (with `GetLastError`), and give-up to `diagnostics.log`. — complete; isolated no-engine and invalid-regular-file probes recorded connect exhaustion, `engine-launch-failed` with `0xd8`, and `engine-unavailable`.

## 2. Bounded connection lifecycle (design D2)

- [ ] 2.1 Add `EngineConnectionState { Starting, Connected, Unavailable }`; change the status callback to carry `(state, privilegedPathActive)`.
- [ ] 2.2 Bound connect attempts to 5 per cycle; park on a retry condition variable when exhausted.
- [ ] 2.3 Add `EngineClient::RetryConnect()`; wake the parked wait from `Stop()`.
- [ ] 2.4 Update `WindowShell` status fan-out (`WM_APP_ENGINE_STATUS` carries state + privileged flag).

## 3. Status chip (design D3)

- [ ] 3.1 `ColumnView::SetEngineStatus(state, privilegedPathActive)` with per-state text.
- [ ] 3.2 Badge click in the `Unavailable` state invokes the retry callback.
- [x] 3.3 Settings dialog no longer auto-opens at launch (remove the unconditional `ShowWindow` from `SettingsDialog::Initialize`; shown on demand via Settings menu / Ctrl+,). — complete; source inspection and rebuilt Debug launch-window evidence are in `evidence/settings-launch-visibility.md`.

## 4. Publish-failure surfacing (design D4)

- [x] 4.1 Add `DirectoryErrorReason::PublishFailed` to `UiProtocol.h` (additive value). — complete; the additive value is present and covered by the publication/freshness evidence in `evidence/publish-failure.md`.
- [x] 4.2 `UiServer::RepublishAndBroadcastGeneration` returns success; `HandleRequestDirectory` sends `PublishFailed` on failure. — complete; source path is covered by `ffengine_snapshot_publisher_tests` and `ffengine_ui_server_lifecycle_tests`.
- [x] 4.3 `ColumnView` maps `PublishFailed` to a clear per-column message. — complete; `ffsnapshot_freshness_tests` covers stale same-context retention, unavailable new contexts, and later-generation recovery; source mapping is recorded in `evidence/publish-failure.md`.

## 5. Watcher safety (design D5)

- [x] 5.1 Bound watched roots (32) with least-recently-watched eviction. — complete; the expanded `ffengine_directory_watcher_tests` proves LRU eviction and diagnostic output; evidence is in `evidence/watcher-safety.md`.
- [ ] 5.2 Overflow recovery: log, re-enumerate via the change callback, re-arm the watch; give up after 8 consecutive overflows with a log entry.

## 6. Search scope honesty (design D6)

- [ ] 6.1 Relabel scope items to describe browsed-locations coverage; retain engine-availability gating.

## 7. Tests and acceptance

- [x] 7.1 Add `ffengine_discovery_tests` (candidate order, dedupe, validation) and register it with CTest. — complete; target is registered and focused CTest passed.
- [x] 7.2 Full build (zero warnings) and full CTest suite pass. — complete; Debug/Analyze builds passed and serial CTest passed 42/42.
- [ ] 7.3 Manual acceptance from the development build tree: launch → engine reachable → drives listed → folder navigation ≥5 levels → back/up navigation → file open → file-operation smoke → search over a browsed folder → missing-engine failure state visible with working retry.
