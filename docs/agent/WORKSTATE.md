# Fast Files — Agent Workstate

Last updated: 2026-08-09T23:32:22+08:00
Status: COMPLETE

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD: `9db73c1`
Working tree: clean after product commit `9db73c1` and its WORKSTATE bookkeeping commit
Infrastructure commit: `4ca9e40` (`Add persistent Codex workstate protocol`)
Product commit: `2d671d1` (`Preserve selection identity across snapshot refresh`)

## Current Engineering Slice

Name: Global shortcut routing while native edit controls own focus

Objective: Keep explicitly global Fast Files shortcuts available from native edit controls while preserving native text-editing ownership and suppressing non-global view/file shortcuts.

Safety / correctness invariant: One key press produces one intended semantic action; global application commands dispatch once, native edit commands remain with the edit control, and view-local commands do not accidentally operate on the file view while text is being edited.

Non-goals: shortcut persistence/settings source-of-truth, Storage Analysis, selection identity, Treemap category filtering, watcher shutdown ordering, stale snapshot UX, MFT/USN, signing/authentication, installer behavior, or general command-system redesign.

## Current Frontier

Root cause is confirmed. `WindowShell::RunMessageLoop` first routes palette/settings messages, then computes `textInput` from `msg.hwnd` (`IsNativeTextInput` class checks for `Edit`/`RichEdit20W`, plus `inlineRename_`). Its broad `!textInput` gate skips both `ShortcutScope::Global` and `ShortcutScope::ActiveView` dispatch. Therefore `Ctrl+F`, `Ctrl+L`, and `Ctrl+Shift+P` never reach the command map from the address-bar `EDIT`, even though their registry bindings are Global. The main loop uses `GetMessageW`/`TranslateMessage`/`DispatchMessageW`; it has no `TranslateAccelerator` path. `SettingsDialog::HandleMessage` is an earlier child-message consumer, so Global routing must be checked before it.

The current registry has Global defaults for `search.focus` (`Ctrl+F`), `navigation.focus-path` (`Ctrl+L`), `app.command-palette` (`Ctrl+Shift+P`), and tab/bookmark commands. Copy/cut/paste/select-all are ActiveView. `file.undo` is the one conflicting default: it binds `Ctrl+Z` as Global even though `Ctrl+Z` is a native edit chord; it must become ActiveView so the edit control retains it.

Decision: retain the existing `Global` / `ActiveView` metadata, add one pure `ShortcutScope` + native-edit-focus eligibility helper, determine native-edit ownership from `GetFocus()` with the queued key message HWND as a fallback (plus inline-rename coverage), and perform Global lookup before palette/settings routing. ActiveView dispatch remains suppressed for native edit focus and palette-visible routing. The `continue` after a successful Global dispatch prevents a second semantic dispatch. `NavigationChrome::EndAddressEdit(false)` now cancels on focus loss without reclaiming focus, so Global search/palette commands can move focus to their own native controls; explicit Escape still restores chrome focus.

OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts remain the planning context. Implementation, focused tests, current-source runtime acceptance, Analyze, full CTest, and the bounded product commit are complete.

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

- None.

### Not Started

- None for this slice.

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

Debug: PASS; rebuilt the current worktree with `cmake --build --preset debug`; the current executable is `build/debug/src/ui/FastFiles.exe` (the separate staged copy remained stale and was not used for acceptance).
Analyze: PASS; `cmake --build --preset analyze` completed with no work after the current-source tree was already analyzed.
CTest: PASS, 39/39 serial with long-form process-local `TEMP=C:\Users\Michael Roy\AppData\Local\Temp` and `TMP=C:\Users\Michael Roy\AppData\Local\Temp`; `ffshortcut_routing_tests` passed as test 34/39.
Runtime (shortcut slice): PASS on the rebuilt current Debug UI via direct Win32 `keybd_event` and live HWND state. `Ctrl+L` focused address `EDIT` HWND 264124. Native acceptance used actual typed `probe` text and `WM_GETTEXTLENGTH`/`EM_GETSEL`: Ctrl+A selected all (0..5), Ctrl+C yielded clipboard `probe`, Ctrl+X reduced edit length to 0, Ctrl+V with `PasteProbe` produced length 10, and Ctrl+Z restored length 5; focus remained the address edit throughout. Global acceptance from the edit passed: Ctrl+Shift+P focused the command-palette `EDIT` HWND 133068 with its ListBox visible, Ctrl+F focused the search `EDIT` HWND 133076, and Ctrl+L returned focus to address `EDIT` HWND 264124. F5 while address edit focus remained on HWND 264124, with no view-routing UI change observed. Escape cleanup completed separately. Storage runtime evidence below is from the completed prior slice.
Runtime (shortcut regression): PASS on a fresh current Debug GUI instance. Real address-edit paste/Enter navigation reached `C:\Windows\Temp` and `C:\Windows`; after each commit the edit hid and focus returned to the main HWND. Ctrl+Shift+P from normal view focus opened the palette `EDIT`/ListBox and Escape closed it; `IsHungAppWindow` reported responsive. No destructive file operation was used.
Runtime (prior Storage slice): PASS. Live Debug FastFiles accepted Overview, Drill Down, Largest Folders, Largest Files, By Category, and Treemap. For each mode, `IsWindowVisible`/`GetWindowRect` was queried before and after a 1500x900 resize; hidden mode-owned HWNDs remained hidden, radio state tracked the active mode, and closing Storage hid every Storage child. The Direct2D Treemap surface had no layout in degraded coverage, so its screenshot was not treated as visual tile evidence; native control visibility and checked Treemap mode were verified.
Other: OpenSpec CLI is unavailable; checked-in `storage-analysis` artifacts were read manually. Build tools are available under the installed VS 2022 Build Tools instance.
Prior product commit: `412e25b` (`Fix Storage Analysis mode visibility`). Current shortcut product commit: `9db73c1fed5c8ed3a96c95e9ef89b463254ce197` (`Preserve global shortcuts while editing text`).

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.
- `cmake` was initially absent from the plain shell PATH; using `Launch-VsDevShell.ps1` from the installed Build Tools instance resolved the harness setup without changing the repository.
- Runtime probe: a read-only PowerShell/PInvoke query used the live main HWND and Storage child control IDs; normal command-palette and radio-button commands drove mode transitions. FastFiles remained responsive and the Engine/UI process pair stayed alive.
- Treemap screenshot capture was limited by the existing degraded empty-layout path (`TreemapView::Render` returns before clearing when `layout_` is empty); no Treemap rendering claim is made from that capture.
- No active blockers remain for this slice; native-edit, global shortcut, and bounded runtime regression acceptance pass on the rebuilt current executable.
- Plain-shell `cmake` is unavailable; focused validation succeeded after launching the installed VS 2022 Build Tools developer shell, as documented above.
- OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts were used as planning context and no unrelated change artifacts were edited.
- The first runtime probe used a stale staged executable and invalid cross-process `GetWindowText`/`EM_GETSEL` observations; those results are explicitly excluded. The rebuilt run used the source-matching executable, actual key-generated text, `WM_GETTEXTLENGTH`, `EM_GETSEL` return values, and clipboard contents.
- Full CTest environment evidence: default 8.3 user TEMP passed `ffindexstore_store_tests` but failed unrelated `fffileoperations_e2e_tests`; `C:\Windows\Temp` passed the file-operations test but aborted the index-store test in a Debug CRT dialog; long-form user TEMP/TMP passed both isolated tests and is the selected full-suite environment.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.
- Storage-specific limitation retained: degraded mode may have no Treemap layout, so visual Treemap tiles were not claimed by this slice; allocated-size metadata remains the existing projection gap.

## NEXT EXACT ACTION

This slice is complete. Do not implement the next slice in this session. The single next recommended slice is Shortcut settings persistence / source-of-truth disconnect.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
