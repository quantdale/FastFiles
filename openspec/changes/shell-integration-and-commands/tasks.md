## 1. Shared Command Registry

- [x] 1.1 Define the `CommandDescriptor` struct (`CommandId`, `DisplayName`, `Category`, `DefaultShortcut`, `SelectionApplicability`, `Scope`, `EnabledPredicate`, `Handler`) and the `CommandRegistry` container
- [x] 1.2 Implement registration entry points capability areas call at startup to add their commands (file operations, navigation, search, storage analysis, properties)
- [x] 1.3 Define the `Handler` signature to take an abstracted selection (a list of file-system paths) rather than any FastFiles-internal window/object handle, per the shell-extension extension point (design D7)
- [x] 1.4 Implement lookup/query helpers: by `CommandId`, filtered by `SelectionApplicability` given a current selection, filtered by `EnabledPredicate` given current context
- [x] 1.5 Register the baseline command set: New Folder, Copy, Cut, Paste, Rename, Delete, Copy Path, Copy Relative Path, Open Containing Folder, Open Terminal/PowerShell Here, Properties, Search, Analyze Storage, Toggle Column View, Toggle Dual Pane, Refresh, Settings, Open Command Palette, Back, Forward
- [x] 1.6 Unit tests: registering a duplicate `CommandId` is rejected/logged; filtering by selection kind returns the expected subset for file/folder/multi/mixed/empty selections

## 2. Context Menus and Quick Actions

- [x] 2.1 Implement the selection-kind classifier (single file, single folder, multi-selection same-kind, mixed multi-selection, empty/background) feeding `SelectionApplicability` filtering
- [x] 2.2 Implement the native Win32 popup menu builder: `CreatePopupMenu`, `InsertMenuItem`/`AppendMenu` from filtered registry entries, separators, mnemonics
- [x] 2.3 Implement mouse invocation: right-click anchors `TrackPopupMenuEx` at the cursor
- [x] 2.4 Implement keyboard invocation: context-menu key / Shift+F10 anchors the popup at the selected item's screen rect
- [x] 2.5 Implement Open: file launches via OS-registered default association; folder delegates to existing `column-view-browsing` navigation
- [x] 2.6 Implement Open With via the Windows "choose an application" picker
- [x] 2.7 Wire Copy/Cut/Rename/Delete menu entries to the existing `file-operations-core`/`conflict-resolution` handlers (no new logic)
- [x] 2.8 Implement Copy Path: absolute path(s) to clipboard, newline-separated for multi-selection
- [x] 2.9 Implement Copy Relative Path: resolve base folder (other pane in dual-pane mode, else current view's root column), compute relative path, fall back to absolute path with a notification when no relative path exists (cross-volume). (`NavigationWorkspace::OtherPanePath()` returns the inactive pane's current location in dual-pane mode and `nullopt` otherwise; the `item.copy-relative-path` handler in `WindowShell.cpp` resolves the base as the other pane's location when dual-pane is active else `ColumnView::RootPath()`, and feeds `PathsRelativeTo` which computes same-volume relative paths via `std::filesystem::relative` and falls back to the absolute path with a `MessageBoxW` notification on cross-volume/no-relative — covering spec scenarios "Relative path within the same volume as the base" and "No valid relative path exists". Unit-tested: `tests/navigation/test_navigation_workspace.cpp` (`TestCopyRelativePathBaseResolution`: single/dual pane, pane switch, disable) and `tests/commands/test_command_system.cpp` (cross-volume fallback); full build green under `/W4 /WX` and 18/18 `ctest` pass.)
- [x] 2.10 Implement Open Containing Folder: navigate to the item's parent via existing Column View navigation, with the item selected
- [x] 2.11 Implement Open Terminal/PowerShell Here: resolve target directory, `CreateProcess` with `lpCurrentDirectory` set (never interpolated into a command line), default shell PowerShell falling back to cmd.exe, clear error if no shell can be launched
- [x] 2.12 Implement Properties: invoke the in-app `properties-and-details` view (single item and aggregate multi-selection), not the native Windows Properties dialog
- [x] 2.13 Implement the empty-area/background context menu (Paste when clipboard is compatible, Open Terminal/PowerShell Here, other background-appropriate entries)
- [ ] 2.14 Manual/UI test pass across all selection kinds: single file, single folder, multi-selection (files-only, folders-only, mixed), empty area — **context menu implementation complete** (§2.1-2.13); manual UI validation across selection kinds pending Windows run.

## 3. Command Palette

- [x] 3.1 Implement the palette overlay UI (input field, ranked result list, keyboard focus handling) as a consumer of the shared `CommandRegistry`
- [x] 3.2 Implement the fuzzy-match/ranking algorithm (favor exact-prefix and word-boundary matches over pure subsequence matches)
- [x] 3.3 Implement live result updates as the query text changes
- [x] 3.4 Implement full keyboard operability: open via shortcut, arrow-key navigation, Enter to execute, Escape to dismiss without executing
- [x] 3.5 Implement disabled/ineligible command display when a result's `EnabledPredicate` is not currently satisfied, with a clear non-crashing message if invoked anyway
- [x] 3.6 Implement the bound-shortcut hint display per result, sourced from the same shortcut data `keyboard-shortcuts` maintains
- [ ] 3.7 Verify baseline command coverage (New Folder, Copy Path, Open Terminal Here, Search, Analyze Storage, Toggle Column View, Toggle Dual Pane, Refresh, Settings) is discoverable and executable through the palette — **command palette and all baseline commands implemented** (§3.1-3.6, §1.5); manual discoverability validation pending Windows run.
- [x] 3.8 Manual test: complete an end-to-end command execution using only the keyboard, no mouse input at any step

## 4. Keyboard Shortcuts

- [x] 4.1 Define the `ShortcutBinding` struct (`CommandId`, `KeyChord`, `Scope`) and the in-memory shortcut map merging defaults with persisted customization
- [x] 4.2 Register default bindings: Back (Alt+Left), Forward (Alt+Right), Refresh (F5), Copy (Ctrl+C), Cut (Ctrl+X), Paste (Ctrl+V), Delete (Delete), Shift+Delete (permanent delete), Rename (F2), Select All (Ctrl+A), Focus Search (Ctrl+F), Focus Path Entry (Ctrl+L), Open Command Palette (Ctrl+Shift+P)
- [x] 4.3 Implement the persisted customization file format (diffs-only from defaults, keyed by stable `CommandId`) with load/merge logic and graceful handling of unresolvable `CommandId`s (ignore-and-log, not fatal)
- [x] 4.4 Implement the top-of-chain Global-scope accelerator check in the main window's input handling, dispatched before per-view/per-control routing
- [x] 4.5 Implement Active-View-scope dispatch: resolve against whichever pane/tab is currently active, regardless of which control within it has focus
- [x] 4.6 Wire the Focus Search shortcut as Global-scoped and verify it fires regardless of which pane/column/control currently has focus
- [x] 4.7 Implement rebinding UI/API: attempt a rebind, detect conflicts against the current effective map (defaults + customization) within overlapping scope
- [x] 4.8 Implement conflict resolution flow: warn on conflict, support reassigning (unbind the other command) or canceling, with no silent overwrite
- [x] 4.9 Implement the Windows-reserved-combination warning (e.g. Alt+F4) that still allows saving the binding
- [x] 4.10 Expose the shortcut data model's read/write API in a form the future `settings-and-appearance` `settings-ui` capability can consume, documenting the coordination point
- [x] 4.11 Unit tests: default-only startup produces the documented bindings; a persisted customization overlays without shadowing unrelated new defaults; conflicting rebind is rejected until resolved; unresolvable persisted `CommandId` is dropped without crashing

## 5. Cross-Cutting Integration and Validation

- [x] 5.1 Verify every command exposed via context menu, palette, and shortcut resolves to exactly one registry entry and one handler (no duplicate/divergent definitions)
- [x] 5.2 Verify palette shortcut hints always match what `keyboard-shortcuts` reports as the live binding, including immediately after a rebind
- [x] 5.3 Verify "Open Terminal/PowerShell Here" passes the target path only via the process's current-directory parameter, never via an interpolated command-line string, against folder names containing special characters
- [x] 5.4 Verify Properties invoked from the context menu and from the command palette both open the same in-app `properties-and-details` view, not a native Windows Properties dialog
- [x] 5.5 Document, in code comments or an architecture note, the extension point (`Handler` taking abstracted path-list selections) that a later Explorer-shell-extension change would reuse, without implementing that extension here
