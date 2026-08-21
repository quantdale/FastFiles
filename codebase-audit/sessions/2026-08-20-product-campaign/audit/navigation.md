# Audit area: NAVIGATION &amp; WORKSPACE STATE — audit src/ui/src/NavigationWorkspace.{h,cpp}, NavigationChrome.{h,cpp}, NavigationSidebar.{h,cpp}, NavigationLocation.h, RecentFolders.{h,cpp}, HomeData.h, src/ui/include/ffui/HomeLayout.h, plus the engine-side degraded enumeration/watching (Glob for DegradedModeEnumerator/DirectoryWatcher under src/engine/). Cover: back/forward history coherence, tab create/switch/close, Home activation rules (Home location path must never reach Win32 APIs), sidebar entries incl. missing targets, breadcrumb/path display, navigation to nonexistent/disconnected/removable paths, permission-denied folders, rapid repeated navigation, refresh behavior, watcher-driven update reconciliation, stale directory listings, dual-pane independence.

(agent agent-0, outcome completed)

## Defects

### [P2] All tabs render with the active-tab styling — active tab is visually indistinguishable — src/ui/src/NavigationChrome.cpp:341
- Symptom (user-visible): with 2+ tabs open, every tab pill is drawn in the "active" style (bright text + `surfaceSubtle` pill); the user cannot tell which tab is current.
- Evidence: `DrawTabButton` derives active state from `const bool active = (item->itemState & ODS_DISABLED) == 0;` (line 341), but `RebuildTabs` explicitly enables every tab: `EnableWindow(tab, TRUE);` (line 242) with the comment "the active tab is already visually distinguished by the owner-draw state". Since no tab is ever disabled, `ODS_DISABLED` is never set and `active` is always true. The comment's premise was broken when inactive-tab disabling was removed; the visual distinction silently died with it.
- Failure scenario: open a second tab, switch between tabs — both pills look identical.
- Root cause hypothesis: the owner-draw "active" signal was `ODS_DISABLED` from the era when inactive tabs were disabled; enabling all tabs for clickability removed the signal without replacing it.
- Suggested fix: pass the real active index into drawing — e.g. in `WM_DRAWITEM`, compute `active = (item->CtlID - kTabBaseId) == workspace_->ActiveTabIndex()` and use that instead of `ODS_DISABLED`.
- Regression test idea: headless test is hard (GDI); at minimum a UIA/name test asserting exactly one tab exposes the active state, or refactor tab-label/state computation into a testable helper returning `{label, isActive}` per index.

### [P2] Address-bar error text survives Back/Forward/tab switches — src/ui/src/NavigationWorkspace.cpp:238
- Symptom (user-visible): after a failed address-bar commit ("This location no longer exists."), pressing Back/Forward or switching tabs keeps painting the stale error over the breadcrumbs of the new location, implying the new location is invalid.
- Evidence: `Navigate()` clears `addressBarError` and resets `addressBarMode` (NavigationWorkspace.cpp:227-228), but `GoBack()` (238-245) and `GoForward()` (247-254) update only `currentPath`/`isHomeLocation`. `DrawBreadcrumbs` unconditionally paints `ActiveContext().addressBarError` (NavigationChrome.cpp:328-333).
- Failure scenario: type a bad path → Enter (error shown) → press Esc-free Back button → error still rendered under the previous folder's breadcrumbs.
- Root cause hypothesis: GoBack/GoForward predate the addressBar fields and were never taught to reset them.
- Suggested fix: in `GoBack`/`GoForward`, also set `context.addressBarMode = AddressBarMode::Breadcrumb; context.addressBarError.clear();` (mirroring `Navigate`).
- Regression test idea: headless `ffnavigation` test — commit an invalid path via `CommitAddressBarPath`, call `GoBack()`, assert `ActiveContext().addressBarError.empty()`.

### [P3] Clicking the server segment of a UNC breadcrumb navigates to a non-enumerable `\\server` — src/ui/src/NavigationWorkspace.cpp:315
- Symptom (user-visible): browsing `\\server\share\sub`, the accent-colored (clickable-looking) `server` crumb navigates to bare `\\server`, which cannot be enumerated → the view flips to an error state.
- Evidence: the plain-UNC branch emits one segment per component starting with the server (`accumulated = L"\\\\"; ... result.push_back({component, accumulated})`, lines 315-329); same for the extended form `\\?\UNC\` (282-296). `NavigateBreadcrumb` calls `Navigate(segments[i].path)` with **no** existence/validation (347-352), unlike the address bar which runs `CommitAddressBarPath`.
- Failure scenario: click the first crumb of any UNC path → engine classifies `ERROR_BAD_NETPATH` as NotFound → "no longer available" error for a crumb the UI offered as navigable.
- Root cause hypothesis: breadcrumb builder treats server and share as independent components; only `\\server\share` (share root) is a valid navigation target per `ParentNavigationPath`'s own comment (809-811).
- Suggested fix: in the UNC branches, merge server+share into the first emitted segment (path `\\server\share`), or make `NavigateBreadcrumb` reject paths that fail `ParseNavigationPath`/a share-root check.
- Regression test idea: unit-test `Breadcrumbs()` for `\\srv\share\a\b`: assert no segment has path `\\srv`; extended-UNC variant likewise.

### [P3] "This PC" breadcrumb segment is drawn but can never be clicked — src/ui/src/NavigationWorkspace.cpp:263
- Symptom (user-visible): the always-present first crumb "This PC" is styled non-clickable and clicking it does nothing, even though the app elsewhere treats This PC as a real destination (Up/Backspace maps drive roots to it via `ParentNavigationPath` returning `""`, line 804-807; address bar accepts literal "This PC", line 690).
- Evidence: `Breadcrumbs()` returns `{L"This PC", L""}` (line 263); `NavigateBreadcrumb` returns false for any empty-path segment (line 349); `DrawBreadcrumbs` styles empty-path segments as non-accent (NavigationChrome.cpp:314-317).
- Failure scenario: user deep in `C:\a\b` clicks "This PC" expecting root — nothing happens; the only routes are Up twice or typing.
- Root cause hypothesis: empty string is overloaded as "no path" so the synthetic root can't be expressed as a crumb target.
- Suggested fix: give the This PC segment a sentinel (e.g. keep path empty but add a `syntheticRoot` flag on `BreadcrumbSegment`) and have `NavigateBreadcrumb` navigate `std::wstring{}` for it.
- Regression test idea: extend the breadcrumb unit test to assert the first segment is marked navigable once the flag exists.

### [P3] Drive dropdown is populated once at startup and never refreshed — src/ui/src/NavigationChrome.cpp:102
- Symptom (user-visible): plug in a USB drive after launch → it never appears in the drive combo; eject a drive → its entry remains and selecting it lands in an error view, until app restart.
- Evidence: `RebuildDrives()` is called only from `Initialize` (line 102); grep of `src/ui` shows no other caller and no `WM_DEVICECHANGE` handling anywhere in the UI layer.
- Failure scenario: standard removable-media workflow — insert stick, open drive dropdown, stick missing.
- Root cause hypothesis: device-arrival handling was never wired to the chrome combo.
- Suggested fix: handle `WM_DEVICECHANGE`/`DBT_DEVNODES_CHANGED` in the shell (or chrome window) and call `RebuildDrives()` + `Refresh()`.
- Regression test idea: extract drive-list refresh into a seam and test that a device-change notification re-reads `EnumerateDrives()`.

### [P3] Bookmarking a drive root produces a blank-named entry — src/ui/src/NavigationWorkspace.cpp:61
- Symptom (user-visible): "Bookmark This PC/drive" on `C:\` creates a sidebar/Home-pinned row whose label is empty (a blank strip).
- Evidence: `DefaultBookmarkName(L"C:\\")`: `end = find_last_not_of(L'\\') == npos`, `slash = find_last_of(L'\\', npos) == 2`, returns `path.substr(3, npos-2)` → `pos == size()` → `L""`; `AddBookmark` accepts it since it only checks the *input* name for emptiness (line 438: `name.empty() ? DefaultBookmarkName(path) : ...` — the fallback result isn't re-checked).
- Failure scenario: bookmark `C:\` → sidebar shows an unlabeled clickable row; Home Pinned shows a blank card.
- Suggested fix: in `DefaultBookmarkName`, return the full path (e.g. `L"C:"`) when no leaf component exists; or in `AddBookmark`, fall back to `path` when the derived name is empty.
- Regression test idea: unit test `AddBookmark(workspace, L"C:\\")` then assert `State().bookmarks.back().displayName` is non-empty.

### [P3] Home snapshot goes stale: recents/bookmarks not refreshed on several navigation paths — src/ui/src/WindowShell.cpp:2190
- Symptom (user-visible): open a new tab (Home) right after browsing — the Recent folders list doesn't include folders just visited; pinned edits made elsewhere may lag too.
- Evidence: `RefreshHomeData()` is called from `NavigateToLocation` (WindowShell.cpp:203) but NOT from the direct model-sync paths: keyboard/column drill-in (`navigationWorkspace_.Navigate(activePath)` at 2190-2193 and 1998-2001), sidebar filesystem navigation (860-865), and the chrome callback's `restoreTabState` branch which returns after `RestoreActiveTabViewState()` without refreshing Home data (821-829) — yet new-tab (`kNewTabId`, NavigationChrome.cpp:584-590) and middle-click Home route through exactly that branch.
- Failure scenario: browse `D:\projects`, Ctrl+T → Home's "Recent folders" predates the visit.
- Root cause hypothesis: `RefreshHomeData` was attached to the single `NavigateToLocation` entry, but several later code paths mutate the model directly.
- Suggested fix: call `RefreshHomeData()` in the chrome callback's `restoreTabState` branch and after the three direct `Navigate(activePath)` sync sites (or centralize: refresh Home data whenever `stateDirty_` recents changed and a Home pane/surface is visible).
- Regression test idea: hard to test headless through WindowShell; extract a `BuildHomeData()` pure helper and assert recents content after simulated `Record`.

### [P3] Ending a session on Home silently disables session restore — src/ui/src/NavigationWorkspace.cpp:673
- Symptom (user-visible): with "restore previous session" enabled, closing the app while a Home tab is active restarts at the default location instead of Home.
- Evidence: `FlushState` writes `lastLocation = ActiveContext().currentPath` (673-677), which is `L""` for Home; `ResolveInitialNavigationPath` requires `!lastLocation.empty()` (NavigationWorkspace.cpp:721) so the persisted preference is ignored.
- Failure scenario: enable restore → navigate Home → close → relaunch → This PC/default, not Home.
- Root cause hypothesis: `lastLocation` is a bare path string that predates typed locations; Home has no representable form.
- Suggested fix: persist a typed marker (e.g. `lastLocationKind: "home"` alongside the existing version-1 JSON, additive like `recentFolders`) and have `ResolveInitialNavigationPath` accept a Home sentinel.
- Regression test idea: round-trip test — serialize a state with the Home marker, deserialize, assert the resolver yields the Home sentinel.

## Verified sound

- **Home invariant**: `NavigationLocation::Home()` always carries an empty path; `MakeContext`, `LocationOf`, `EnableDualPane`, `CloseActiveTab`/`ReopenClosedTab`, and tab labels all go through the typed location; the shell guards Home targets before any Win32 call (WindowShell.cpp:834-854). No path found where Home reaches a filesystem API.
- **History coherence**: `Navigate` truncates forward history correctly, kind-aware same-location no-op prevents churn, and Home↔filesystem transitions are never treated as no-ops (NavigationWorkspace.cpp:214-236).
- **Address-bar validation**: reserved-device namespaces rejected, env-string expansion, reserved chars, `GetFullPathName` normalization, trailing-separator handling for extended roots, and access-denied-vs-not-found distinction in `CommitAddressBarPath` are all correct.
- **`ParentNavigationPath`**: drive-root→This PC, UNC share-root collapse, extended-drive and extended-UNC edge cases all handled consistently.
- **Persistence robustness**: atomic temp-file + `MoveFileExW` save, version gate, malformed-entry skipping in `DeserializeState`, legacy string / typed Home closed-tab forms, UTF-8 conversion with `MB_ERR_INVALID_CHARS`.
- **Watcher reconciliation**: overflow (zero-byte completion and `ERROR_NOTIFY_ENUM_DIR`) re-enumerates with bounded give-up; terminal errors fire one final refresh; overlapped byte-count read via `GetOverlappedResult` (the documented past bug is fixed); watch map is case-insensitive (`CaseInsensitivePathLess`) with LRU eviction; deleted directories publish a NotFound observation rather than resurrecting stale listings (UiServer.cpp:354-371, 645-691).
- **Degraded enumeration**: `.`/`..` skipped, 64-bit size/time assembly correct, open errors classified (access-denied vs not-found vs IO), extended/UNC path prefixing correct.
- **Dual-pane model**: pane contexts are independent per tab, `ActivatePane` clamps, `DisableDualPane` keeps the active pane, `OtherPanePath` guarded to exactly two panes.
