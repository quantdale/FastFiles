# Fast Files — Agent Workstate

Last updated: 2026-08-10T06:47:15+08:00
Status: BLOCKED

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD: `e9fa6e4`
Working tree: product shortcut-persistence changes and this WORKSTATE checkpoint are uncommitted; prior routing/bookkeeping commits remain clean at HEAD
Infrastructure commit: `4ca9e40` (`Add persistent Codex workstate protocol`)
Product commit: `2d671d1` (`Preserve selection identity across snapshot refresh`)

## Current Engineering Slice

Name: Shortcut settings persistence / single source of truth

Objective: Make Settings persistence and live command routing consume one authoritative shortcut configuration, with deterministic migration, live apply, restart persistence, and no regression of native-edit/global scope behavior.

Safety / correctness invariant: Settings changes validate, persist, update the live command model, survive restart, and preserve Global versus ActiveView routing semantics.

Non-goals: new shortcut customization features, Settings redesign, scope semantics changes, Storage Analysis, selection identity, Treemap category filtering, watcher shutdown ordering, stale snapshot UX, MFT/USN, signing/authentication, installer behavior, or general command-system redesign.

## Current Frontier

Reconciled from clean HEAD `e9fa6e4`; the previous routing slice is complete. Source mapping is complete. The broken edge is SettingsDialog's independent `ffprotocol::Settings::shortcuts` vector: it displays/saves `(command text, human binding text)` inside `settings.json`, while WindowShell loads `ShortcutMap` from a separate `shortcuts.json` and never consumes that vector. Settings OK calls `SaveAndNotifySettings`, which saves only `settings.json`; the live map is unchanged, so the UI can report a saved shortcut that routing ignores.

Data-flow map:
`CommandSystem` registry defaults (`CommandDescriptor::defaultShortcut` + `scope`) -> `ShortcutMap::ResetDefaults` -> canonical `shortcuts.json` diff load -> WindowShell dispatch / CommandPalette hints.
The broken path was `SettingsDialog` -> `Settings::shortcuts` -> `settings.json`; it bypassed `ShortcutMap` entirely.

Persisted formats:
- Canonical `shortcuts.json`: version/bindings array; each customization is keyed by stable `commandId` and stores numeric `key` + `modifiers` (`CSA`), or `unbound`; scope is restored from current registry metadata, not persisted.
- Legacy `settings.json`: nested `shortcuts.bindings` array of `command` + human-readable `binding`; it cannot represent scope/unbound state and is not suitable as the runtime model.
- Settings JSON is `%LOCALAPPDATA%\\FastFiles\\settings.json`; the dedicated file is `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`; both currently use the same AppDirectory convention but different loaders/writers.

Canonical source: dedicated `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`, because it is the existing runtime model and preserves stable IDs, unbound state, conflict behavior, and registry-derived Global/ActiveView scope.
Runtime in-memory source: the one WindowShell-owned `ShortcutMap`, shared with CommandPalette and SettingsDialog.
Legacy source: `settings.json`'s old `shortcuts.bindings` section, migration-only.
Migration policy: if canonical `shortcuts.json` exists, it wins (including an intentionally empty bindings array); if it does not exist, parse valid legacy entries, resolve command IDs/display names against the registry, apply valid bindings deterministically, save canonical once, and ignore/log unknown or malformed entries. Missing legacy data uses registry defaults. No later startup reads legacy shortcuts once canonical exists.
Live apply policy: SettingsDialog edits a draft of the shared ShortcutMap; accepted changes replace the WindowShell map, save canonical `shortcuts.json`, and refresh the existing CommandPalette pointer immediately. Settings/general persistence remains separate and no longer carries shortcut values.

Implementation milestone: `Settings` no longer owns or writes shortcut values; `ffprotocol::ShortcutSettingsPath` is shared by startup and save paths; WindowShell loads canonical data or performs one-shot legacy import; SettingsDialog lists/edits the real command map through a draft; `ShortcutMap` validates canonical JSON, rejects duplicate/conflicting/invalid persisted bindings, preserves registry-derived scope, and saves atomically.

OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts remain the planning context. Source mapping, implementation, actual Settings UI edit/apply/live/restart acceptance, native-edit/global regression, Debug/Analyze builds, and full CTest are complete. Only final staging/commit bookkeeping remains. No product commit was created.

## Established Facts

- `Column` owns independent `selectedIndices`, `selectionAnchor`, and `focusIndex`; `columns_` and `columns2_` provide separate pane state.
- `RefreshColumnFromSnapshot` rebuilt/sorted `column.items`, restored selection by case-insensitive name, and could leave `focusIndex` at an unrelated row when the focused item disappeared.
- `ActiveSelectionPaths`, `CurrentSelectionSummary`, command dispatch, drag start, open, rename, delete, copy, cut, move/paste, and context actions resolve paths from the active column's current selected indices.
- Degraded enumeration entries carry no stable file ID; indexed snapshot entries carry `volumeRowId`, `fileIdLow`, and `fileIdHigh` from the existing index projection.
- Indexed identity is valid only when both a nonzero volume row and nonzero FRN data are present. Degraded identity is normalized full path, compared case-insensitively with Windows ordinal comparison.
- If both sides have stable indexed identity, matching requires volume row plus FRN. If either side has stable indexed identity and the other does not, no path downgrade is allowed; path matching is used only when neither side has stable identity.
- Error/missing directory refreshes now clear items, selection, anchor, and focus.
- Search-result pending selection still uses the existing path/name navigation mechanism; the refreshed column state now uses the same identity-safe remapping once entries are materialized.
- Debug-only `WM_COPYDATA` observation reads the active path, visible row names, selected/focused identity and row, and the existing `ActiveSelectionPaths()` operation target; it has no selection-control path.

## Decisions

- decision: Add a small header-only `ffui` identity/capture/restore seam instead of a global selection subsystem.
  rationale: Reuse existing index identity fields and current path data while keeping the behavior headless and deterministic.
  consequence: `ColumnView` retains index-based rendering/storage, but indices are regenerated only after identity matching.
- decision: Preserve indexed rename continuity only when volume+FRN proves continuity.
  rationale: A same-path replacement must not inherit an old indexed selection.
  consequence: Degraded mode follows full path; indexed-to-degraded downgrade clears safely.
- decision: Keep infrastructure and product commits separate.
  rationale: The workstate protocol must survive independently from the unfinished product slice.
  consequence: Commit only `AGENTS.md` and `docs/agent/WORKSTATE.md` first.

## Implementation State

### Completed

- Root cause trace across ColumnView, snapshot projection, degraded enumeration, search selection, and operation-target resolution.
- `src/ui/include/ffui/SelectionIdentity.h` identity and remapping seam.
- `ColumnView::RefreshColumnFromSnapshot` identity capture/restore for selection, anchor, and focus.
- Safe clearing on AccessDenied, NotFound, I/O error, and directory error callbacks.
- Eight focused deterministic regression scenarios in `tests/ui/test_selection_identity.cpp`.
- CTest registration in `tests/ui/CMakeLists.txt`.
- `src/ui/include/ffui/SelectionDiagnostics.h`, `ColumnView::CurrentSelectionDiagnostics`, and `tests/ui/selection_identity_runtime_probe.cpp` provide the minimal read-only runtime observation seam.
- Storage Analysis visibility policy, HWND application point, and focused headless regression tests are complete.

### In Progress

- Run final Debug/Analyze builds and full serial CTest, audit scope, then create the focused shortcut-persistence product commit and complete bookkeeping.

### Not Started

- Product commit and final COMPLETE bookkeeping after all required acceptance passes.

## Files Changed In Prior Storage Slice

- `src/ui/include/ffui/StorageVisibility.h`
  - Pure Storage mode/control visibility policy.
- `src/ui/src/StorageAnalysis.h` and `src/ui/src/StorageAnalysis.cpp`
  - Single `ApplyModeVisibility` owner; refresh is data-only and reposition is geometry-only.
- `tests/ui/test_storage_visibility.cpp` and `tests/ui/CMakeLists.txt`
  - Headless mode matrix, transitions, resize-policy stability, and idempotence coverage.
- `docs/agent/WORKSTATE.md`
  - Durable frontier and validation checkpoint.

## Files Changed In This Slice

- `src/protocol/include/ffprotocol/Settings.h` and `src/protocol/src/Settings.cpp`
  - Removed shortcut vectors from general Settings persistence, exposed the shared canonical shortcut path, and added one-shot legacy extraction.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Made the runtime ShortcutMap the validated canonical model, with atomic save/load, stable IDs, scope-derived bindings, and legacy import.
- `src/ui/src/SettingsDialog.h` and `src/ui/src/SettingsDialog.cpp`
  - Settings Shortcuts page now edits a draft of the live ShortcutMap and applies it on OK.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - Startup canonical load/legacy migration and save/live-apply wiring.
- `tests/commands/test_command_system.cpp` and `tests/protocol/test_protocol.cpp`
  - Persistence, malformed/unknown, scope, path, and legacy migration coverage.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Central native-edit eligibility policy; `file.undo` is ActiveView so Ctrl+Z remains native while editing.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - GetFocus/message-target native-edit routing; Global dispatch runs before child-dialog routing and consumes once.
- `src/ui/src/NavigationChrome.h` and `src/ui/src/NavigationChrome.cpp`
  - Focus-loss cancellation no longer steals focus from a Global command's destination edit.
- `tests/ui/test_shortcut_routing.cpp` and `tests/ui/CMakeLists.txt`
  - Headless actual-binding matrix, edit suppression, non-edit regression, unknown chord, and no-double-route coverage.
- `docs/agent/WORKSTATE.md`
  - Durable routing root cause, validation, and completion checkpoint.

## Tests Added / Modified

- `ffstorage_visibility_tests`
  purpose: Covers all six modes, active-mode/exclusive-control visibility, requested transitions, resize stability, and idempotence.
  status: PASS (focused and full CTest).
- `ffshortcut_routing_tests`
  purpose: Covers Global bindings from native edit focus, native Ctrl+C/V/X/A/Z ownership, view-local suppression, non-edit routing, unknown chords, and one-dispatch behavior.
  status: PASS (focused).

## Validation

Debug: PASS; `cmake --build --preset debug` rebuilt the current worktree and linked `build/debug/src/ui/FastFiles.exe`.
Analyze: PASS; `cmake --build --preset analyze` completed cleanly with `/analyze`.
CTest: PASS, 39/39 serial with process-local `TEMP=C:\Users\Michael Roy\AppData\Local\Temp` and `TMP=C:\Users\Michael Roy\AppData\Local\Temp`; the first run's only failure was corrected documented mixed line endings, and the final run passed all 39.
Focused persistence/routing: PASS — `ffprotocol_tests`, `ffcommand_tests`, and `ffshortcut_routing_tests`; tests cover missing defaults, save/reload, old binding removal, Global/ActiveView scope retention, malformed canonical fallback, unknown commands, legacy import, invalid chords, and settings path/source separation.
Runtime (Settings persistence acceptance): PASS — actual FastFiles Settings HWND `FastFilesSettingsDialog` opened via physical `Ctrl+,`; Shortcuts tab selected by native mouse plus five Right-arrow inputs; stock `SysListView32` row 24 (`Search`) entered its temporary native `EDIT` through physical double-click. Literal `F6` was observed in that editor via cross-process `GetWindowText`, committed with Enter, and applied through the real OK button control path. Canonical file contained `search.focus` key 117 with no Ctrl+F binding; legacy `settings.json` shortcut section was removed. Actual F6 focused Search query EDIT live. After Ctrl+L focused address EDIT `20776946`, old Ctrl+F left address focus unchanged. A normal close/relaunch loaded the same F6 binding; fresh Search query EDIT `3999782` focused on F6, and fresh address EDIT `133296` retained focus after old Ctrl+F.
Runtime (native-edit/global regression): PASS — from the native address EDIT, F6 focused Search; Ctrl+L focused the address EDIT; Ctrl+Shift+P opened command-palette EDIT `723122` plus ListBox `329884`, Escape closed it; F5 while address EDIT owned focus left it focused. Disposable `probe` text exercised native Ctrl+A/C/X/V/Z: selection result `327680` (0..5), copied clipboard `probe`, cut length 0, paste length 5, undo length 5. Original Unicode clipboard text was restored.
Runtime (shortcut slice): PASS on the rebuilt current Debug UI via direct Win32 `keybd_event` and live HWND state. `Ctrl+L` focused address `EDIT` HWND 264124. Native acceptance used actual typed `probe` text and `WM_GETTEXTLENGTH`/`EM_GETSEL`: Ctrl+A selected all (0..5), Ctrl+C yielded clipboard `probe`, Ctrl+X reduced edit length to 0, Ctrl+V with `PasteProbe` produced length 10, and Ctrl+Z restored length 5; focus remained the address edit throughout. Global acceptance from the edit passed: Ctrl+Shift+P focused the command-palette `EDIT` HWND 133068 with its ListBox visible, Ctrl+F focused the search `EDIT` HWND 133076, and Ctrl+L returned focus to address `EDIT` HWND 264124. F5 while address edit focus remained on HWND 264124, with no view-routing UI change observed. Escape cleanup completed separately. Storage runtime evidence below is from the completed prior slice.
Runtime (shortcut regression): PASS on a fresh current Debug GUI instance. Real address-edit paste/Enter navigation reached `C:\Windows\Temp` and `C:\Windows`; after each commit the edit hid and focus returned to the main HWND. Ctrl+Shift+P from normal view focus opened the palette `EDIT`/ListBox and Escape closed it; `IsHungAppWindow` reported responsive. No destructive file operation was used.
Runtime (prior Storage slice): PASS. Live Debug FastFiles accepted Overview, Drill Down, Largest Folders, Largest Files, By Category, and Treemap. For each mode, `IsWindowVisible`/`GetWindowRect` was queried before and after a 1500x900 resize; hidden mode-owned HWNDs remained hidden, radio state tracked the active mode, and closing Storage hid every Storage child. The Direct2D Treemap surface had no layout in degraded coverage, so its screenshot was not treated as visual tile evidence; native control visibility and checked Treemap mode were verified.
Other: OpenSpec CLI is unavailable; checked-in `storage-analysis` artifacts were read manually. Build tools are available under the installed VS 2022 Build Tools instance.
Prior product commit: `412e25b` (`Fix Storage Analysis mode visibility`). Prior shortcut-routing product commit: `9db73c1fed5c8ed3a96c95e9ef89b463254ce197` (`Preserve global shortcuts while editing text`). Current shortcut-persistence product commit: not created.

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.
- `cmake` was initially absent from the plain shell PATH; using `Launch-VsDevShell.ps1` from the installed Build Tools instance resolved the harness setup without changing the repository.
- Runtime probe: a read-only PowerShell/PInvoke query used the live main HWND and Storage child control IDs; normal command-palette and radio-button commands drove mode transitions. FastFiles remained responsive and the Engine/UI process pair stayed alive.
- Treemap screenshot capture was limited by the existing degraded empty-layout path (`TreemapView::Render` returns before clearing when `layout_` is empty); no Treemap rendering claim is made from that capture.
- The first UI driver pass targeted row 22 (`Toggle Details Pane`) because the registry has 15 pre-Search rows, not 14; it added only an unbound customization for a command with no default. This test-profile state was removed by restoring the backed-up user files; no product source changed.
- Win32 runtime driver required a foreground handoff because each shell invocation left the terminal foreground; `GetForegroundWindow` + returned thread IDs from `GetWindowThreadProcessId` + `AttachThreadInput`/`SetForegroundWindow` made the product interactive. Cross-process pointer messages (`TCM_GETITEMRECT`) were abandoned after a timeout; no unsafe target-buffer observation was used.
- FastFiles UI closed through the native title-bar close region on each acceptance run; the exact Engine child remained orphaned after its UI parent exited and was stopped by PID after parent exit. This is runtime-process cleanup evidence, not a shortcut persistence failure.
- Plain-shell `cmake` is unavailable; focused validation succeeded after launching the installed VS 2022 Build Tools developer shell, as documented above.
- OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts were used as planning context and no unrelated change artifacts were edited.
- The first runtime probe used a stale staged executable and invalid cross-process `GetWindowText`/`EM_GETSEL` observations; those results are explicitly excluded. The rebuilt run used the source-matching executable, actual key-generated text, `WM_GETTEXTLENGTH`, `EM_GETSEL` return values, and clipboard contents.
- Full CTest environment evidence: default 8.3 user TEMP passed `ffindexstore_store_tests` but failed unrelated `fffileoperations_e2e_tests`; `C:\Windows\Temp` passed the file-operations test but aborted the index-store test in a Debug CRT dialog; long-form user TEMP/TMP passed both isolated tests and is the selected full-suite environment.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.
- Storage-specific limitation retained: degraded mode may have no Treemap layout, so visual Treemap tiles were not claimed by this slice; allocated-size metadata remains the existing projection gap.

## Runtime Acceptance Checkpoint

- Settings control facts: top-level `FastFilesSettingsDialog`; tab index 5 is Shortcuts; shortcut rows are a native `SysListView32`; `NM_DBLCLK` on subitem 1 creates a temporary native `EDIT`; Enter/blur commits and Escape cancels; IDOK runs `FinishShortcutEdit`, `SaveCurrentPage`, and the real `onChanged_` callback.
- Test binding: `Search` old `Ctrl+F`, test `F6`. Search is registry row 24 (file operations 9 + navigation/selection 15).
- Acceptance evidence: canonical `shortcuts.json` has `{"commandId":"search.focus","key":117,"modifiers":""}` and no `Ctrl+F`; physical F6 focused Search query EDIT `5441536`; physical Ctrl+L focused address EDIT `20776946`; physical old Ctrl+F left address EDIT focus unchanged.
- Completed: normal close, relaunch without rewriting config, F6/old Ctrl+F restart checks, native-edit/global regression, exact user-profile restoration, generated canonical cleanup, and temporary-directory removal. Remaining gate is final repository validation and commit.

## NEXT EXACT ACTION

Run final Debug/Analyze builds and full serial CTest, inspect Git scope, stage only shortcut-persistence product/test changes plus WORKSTATE, verify the staged diff, commit `Unify shortcut settings persistence`, then mark this slice COMPLETE and recommend exactly one next slice without implementing it.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
