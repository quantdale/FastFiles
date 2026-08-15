## Context

FastFiles is a Windows-only C++20 file manager. The UI (`FastFiles` target, sources in `src/ui/src/`) is one process: a Direct2D/DirectComposition surface (`Renderer` + `ColumnView`) plus GDI owner-drawn children (`NavigationSidebar`, `NavigationChrome`, `SearchPanel`, `CommandPalette`, `SettingsDialog`). All navigation state lives in `ffui::NavigationWorkspace` (`src/ui/src/NavigationWorkspace.{h,cpp}`): per-tab `NavigationContext`s (one per pane) hold `currentPath` (`std::wstring`), per-column view state, a per-context back/forward `history` vector + `historyIndex`, and address-bar mode. The empty string is the de-facto sentinel for the synthetic "This PC" root. There is no typed location abstraction today.

Persistence: `%LOCALAPPDATA%\FastFiles\workspace-state.json`, hand-rolled JSON (`version: 1`, `lastLocation`, four collapse flags, `bookmarks[]`, `recentlyClosedTabs[]`), atomic `.tmp`+`MoveFileExW` writes, a 500 ms `FlushState` timer plus shutdown flush, `HasSupportedVersion` gate (unsupported version → defaults, never blocks launch). Bookmarks are `struct Bookmark { path, displayName }` in `WorkspaceState`. Known folders come from `NavigationWorkspace::EnumerateKnownFolders()` (`SHGetKnownFolderPath`, `KF_FLAG_DEFAULT`, cached, refreshed on `WM_SETTINGCHANGE`; unresolvable folders yield `{L"", name}` and render disabled). Search history is `ffsearch::SearchHistory` (TSV file, cap 100, dedupe-to-front, `SetRecordingEnabled` gated by `Settings::retainSearchHistory`), owned by `SearchPanel`.

Navigation flow: model (`Navigate`/`GoBack`/`GoForward`) and view (`ColumnView::NavigateToPath`/`NavigateToComputerRoot`) are kept in lockstep by `WindowShell` (`NavigateWorkspace`/`NavigateColumnViewTo`). `WindowShell` reconciles model↔view whenever `columnView_.ActivePanePath() != ActiveContext().currentPath` (command dispatch, mouse-down, key-down). Sidebar rows are rebuilt wholesale on `Refresh()` from a flat `std::vector<Row>`; activation goes through `NavigateHandler(NavigationTarget{path, displayName, openInNewTab})` bound in `WindowShell.cpp:732-750`. New tab (`OpenTab()` no-arg, Ctrl+T and chrome `+`) clones the active context's path; the per-context history of a new tab is seeded with that clone. There is no new-window command. `ColumnView` renders per-pane columns, dual-pane aware, DIP-scaled, theme/high-contrast gated; `IconCache` provides async system icons via `WM_APP_ICON_READY`.

See `proposal.md` for motivation. Specs: `openspec/changes/quick-access-home/specs/quick-access-home/spec.md`.

## Goals / Non-Goals

**Goals:**
- Home as a typed, first-class navigation state that can never be passed to Win32 filesystem APIs as a path.
- Home reachable from the sidebar, keyboard, and command system; the default for new tabs.
- Home sections: Pinned (existing bookmarks), Recent folders (new bounded persisted history), Recent searches (existing search history), Common locations (existing known folders), Quick actions (existing commands).
- Full degraded-mode usability with zero privileged-service dependency; keyboard-first; instant; local-only; bounded history.
- Data freshness without polling: Home updates when bookmarks, recents, searches, or known folders change.
- Headless testability: location model, recent-folder store, and layout math are pure seams.

**Non-Goals:**
- No second favorites database, no new persistence system, no SQLite, no new settings beyond necessity (recent-folder history reuses the workspace-state JSON and is governed by existing retention conventions; `retainSearchHistory` remains search-only).
- No new-window command (none exists today; not in scope to add).
- No telemetry, network, cloud, or privileged/service changes.
- No new fonts, icon assets, gradients, or arbitrary colors; existing token set only.

## Decisions

### D1: Typed navigation location — `NavigationLocation`, minimal churn

Introduce `struct NavigationLocation` (new header `src/ui/src/NavigationLocation.h`):

```cpp
struct NavigationLocation {
    enum class Kind { FileSystem, Home };
    Kind kind = Kind::FileSystem;
    std::wstring path;                 // valid only when kind == FileSystem;
                                       // empty + FileSystem == "This PC" root
    static NavigationLocation FromPath(std::wstring path);
    static NavigationLocation Home();
    bool IsHome() const { return kind == Kind::Home; }
    bool IsFileSystem() const { return kind == Kind::FileSystem; }
    bool operator==(const NavigationLocation&) const = default;
};
```

`NavigationContext` gains `bool isHomeLocation = false;` alongside the existing `currentPath` (which remains the canonical filesystem path, empty for both Home and "This PC"), and `history` becomes `std::vector<NavigationLocation>` so back/forward can restore Home correctly. `currentPath` keeps its exact meaning for every existing consumer (address bar seeding, breadcrumbs, sidebar highlighting, reconciliation); Home simply has an empty `currentPath` plus the flag, and typed consumers (history, back/forward, sidebar target, chrome breadcrumbs) carry the kind.

**Why:** every existing consumer reads `currentPath` as a filesystem path; replacing the field outright would churn dozens of call sites for no behavioral gain. The flag + typed history give Home first-class status exactly where it matters: history, back/forward, and navigation entry points. **Alternatives considered:** (a) replacing `currentPath` with a `NavigationLocation` everywhere — cleaner in principle, but a wide, risky diff across address-bar commit, breadcrumbs, `ColumnView`, file operations, and reconciliation for zero user-visible difference; (b) magic sentinel strings (`L"home:"`) — rejected: the mission forbids fake paths, and sentinels can leak into Win32 calls.

Guards: `Navigate`, `GoBack`, `GoForward`, `OpenTab`, `TabPath`, `Breadcrumbs`, and the sidebar/chrome targets all become kind-aware. `ColumnView::ActivePanePath()` returns `L""` while the pane is Home (same as This PC), so file-operation destinations and reconciliation comparisons are naturally safe: `"" == ""` keeps the model in sync, and file ops guard on empty already.

### D2: Home must never reach Win32 filesystem APIs

Audit-driven rule: any code path that calls a filesystem API (`GetFileAttributesW`, `SHGetFileInfoW`, engine `RequestDirectory`, `std::filesystem`, `CreateFileW`, ...) must only ever receive `location.path` after `location.IsFileSystem()` is checked; Home paths are empty strings and therefore rejected by existing empty-path guards. The new surface feeds navigation through `NavigateWorkspace(path)`/`NavigateToPath` with `path.empty()` short-circuits already in place (`WindowShell.cpp:163-176`), and Home activation in `ColumnView` issues no engine request. Regression tests assert `NavigationLocation::Home().path.empty()` and that a Home-flagged context never produces a non-empty path.

### D3: Recent folders — new bounded store in the existing workspace JSON

New headless class `ffui::RecentFolderHistory` (`src/ui/src/RecentFolders.{h,cpp}`, compiled into `ffnavigation_tests` like `NavigationWorkspace.cpp`):

- Storage: `std::deque<std::wstring>`, persisted cap `kMaxRecentFoldersPersisted = 50`, Home display cap `kHomeRecentFoldersShown = 10` (exposed as constants).
- Recording: `Record(path)` — dedupe-to-front, trim tail. Called from `NavigationWorkspace::Navigate(const NavigationLocation&)` only when `IsFileSystem() && !path.empty()`. This automatically excludes: selection-only changes (no `Navigate`), scrolling (no `Navigate`), opening Home (kind != FileSystem), "This PC" (empty path). GoBack/GoForward do not record (the folder was recorded when first visited).
- **Path identity (exact equality policy):** lexical normalization only — never touch the filesystem. Normalize: trim trailing `\` except a 2-char drive root (`C:\`) and share roots (`\\server\share`); compare case-insensitively with `CompareStringOrdinal(..., TRUE)` (existing repo convention, see `AddBookmark`). UNC and extended-length (`\\?\`, `\\?\UNC\`) paths are compared on the same lexical basis after normalization; no server/share case resolution is attempted. Persisted entries keep the first-seen spelling.
- Persistence: additive JSON array `"recentFolders"` in `workspace-state.json`; `version` stays `1`. Missing field → empty list; malformed/over-long individual entries are skipped, never fatal; corrupted document falls back per existing `LoadState` behavior. Writes ride the existing dirty-flag/500 ms flush/shutdown-flush machinery. `ClearRecentFolders()` clears + marks dirty + returns the state so the caller can refresh Home immediately.
- API: `Record`, `Clear`, `Entries()` (persisted order), `RecentForHome(size_t maxCount)` — and it is a member of `NavigationWorkspace` (not `WorkspaceState`) so recording can be a private hook inside `Navigate` while accessors stay public.

**Why in the workspace JSON:** the mission requires reuse of existing workspace-state infrastructure; the file already carries session-scale lists (`recentlyClosedTabs`), is versioned, atomic, debounced, and flushed on shutdown. **Alternatives considered:** (a) a new file — rejected: extra file to migrate/clean and no benefit; (b) SQLite — rejected: disproportionate for ≤50 strings and forbidden by the mission; (c) a new setting — avoided: no new setting is needed since recording is unconditional and bounded (privacy story: local, bounded, clearable; the existing `retainSearchHistory` setting governs search only, per mission).

### D4: Home surface renders inside the existing ColumnView pane

`ColumnView` gains per-pane Home mode: `void SetPaneHome(int paneIndex, bool home)` and `bool IsPaneHome(int paneIndex) const` (`std::array<bool,2> homePane_`). When a pane is Home, `Render` draws the Home surface in that pane's rect instead of columns; `OnMouseDown`/`OnMouseMove`/`OnKeyDown` hit-test Home items; `ActivePanePath()` returns `L""`; `SavePaneState`/`RestorePaneState` skip column state for Home panes (WindowShell already treats empty `columnPaths` as "re-navigate from `currentPath`", which for Home means staying Home).

Data flows one way: `struct HomeData { std::vector<ffui::Bookmark> pinned; std::vector<std::wstring> recentFolders; std::vector<std::wstring> recentSearches; std::vector<ffui::Bookmark> knownFolders; }` + `ColumnView::SetHomeData(const HomeData&)`. `WindowShell` builds it from `workspace.State().bookmarks`, `workspace.RecentForHome()`, `searchPanel_.RecentQueries()`, `workspace.EnumerateKnownFolders()` and pushes it on every refresh point (§D8). Activation: `ColumnView::SetHomeActivateCallback(std::function<void(const HomeActivation&)>)` where `HomeActivation { enum class Kind { Folder, Search, Action }; Kind kind; std::wstring value; bool openInNewTab; }`; `WindowShell` routes Folder → navigate (new tab if flagged), Search → `searchPanel_.ExecuteQuery(value)`, Action → the corresponding command (`navigation.new-tab`, `search.focus`, `navigation.focus-path`, `app.settings`).

**Why in ColumnView:** Home belongs to a pane and must compose with dual-pane, DPI, theme, device loss, keyboard, and hover exactly like columns do; a separate window/renderer would duplicate all of that (forbidden: "no separate navigation framework"). **Alternatives considered:** (a) separate `HomeView` window child — rejected (parallel chrome, focus, and theme plumbing); (b) rendering Home in `WindowShell::Render` overlay — rejected (input routing and dual-pane geometry would be ad hoc).

Rendering rules (from existing conventions): section headers in `textSecondary`; item rows with hover overlay (`UiFillHoverOverlay`), selection pill (`surfaceSubtle`/`selectionSoft` per sidebar/column conventions), folder icons via `IconCache::Prefetch`/`Get` with glyph fallback; focused item outline in `focusStroke` (the existing but currently unused token — this feature gives it its first consumer, drawn as a 1-DIP rounded rect, suppressed in High Contrast which uses system colors instead); High Contrast suppresses overlays and uses system colors like the sidebar; all metrics from `UiMetrics` in DIPs (D2D context is DPI-scaled already).

### D5: Responsive layout — headless `HomeLayout` seam

New pure header `src/ui/include/ffui/HomeLayout.h` (pattern: `ShellLayout.h`/`DetailsLayout.h` — structs + a `ComputeHomeLayout` function, no dependencies beyond `windows.h` types):

```cpp
struct HomeLayoutInput { float viewportWidth; float viewportHeight; int pinnedCount; int recentFolderCount; int recentSearchCount; int knownFolderCount; bool showActions; };
struct HomeLayoutResult { float contentX, contentY, contentWidth; int columnCount; std::vector<HomeSectionLayout> sections; std::vector<HomeItemLayout> items; };
struct HomeLayoutResult ComputeHomeLayout(const HomeLayoutInput&);
```

Behavior: NARROW (< ~480 DIP) → one column of full-width rows; NORMAL → one or two columns of cards; WIDE → content width capped at a bounded maximum (e.g. `kHomeMaxContentWidth = 960` DIP) centered left-aligned, never stretching items across a whole monitor. Sections stack vertically with headers; items lay out left-to-right, top-to-bottom, bounded by the section's clip rect. `HomeLayoutResult` also exposes each item's rect + its global index so hit-testing and keyboard focus share one source of truth. Contract: no rectangle overlap for any input (tested), zero-count sections produce no items, counts are already bounded upstream (≤10 recents, ≤100 searches etc.) but layout also clamps defensively. Tests: `tests/ui/test_home_layout.cpp` (narrow/normal/wide, 100% and 150% DPI scale inputs, zero sections, large bounded counts, no-overlap sweep).

### D6: Sidebar, chrome, and command entry points

- **Sidebar:** `Row` gains `bool home = false`; `RebuildRows` inserts a single Home row before the Drives section (section index `-1`-style headerless item; keep it simple: an item row with `home=true`, label `L"Home"`, no path). `NavigationTarget` gains `bool isHome = false`. `WM_LBUTTONDOWN`/`WM_MBUTTONDOWN` on a home row → `navigate_({.isHome = true, .openInNewTab = middleClick})`. Selected state: row is current when `workspace->ActiveContext().isHomeLocation`. Keyboard in the sidebar is mouse-only today; the Home row follows existing sidebar conventions (no new sidebar keyboard framework; Home's keyboard model lives in the pane, D7).
- **Chrome:** `Breadcrumbs()` returns a single `{L"Home", L""}` segment when the active context is Home (non-clickable, like the "This PC" segment). Tab labels: `TabPath`-based rendering shows `L"Home"` for Home tabs (chrome reads the new typed `TabLocation`). `BeginAddressBarEdit` from Home seeds an empty address (same as This PC); commit semantics unchanged (empty commit is rejected by the existing parser — documented behavior).
- **Commands:** new `navigation.home` (binding `Alt+Home`, matching Explorer convention; `CommandSystem::RegisterNavigationCommands`) → handler navigates the active context to Home. `navigation.new-tab` and the chrome `+` call `OpenTab()` whose default becomes Home.

### D7: New-tab default is Home (deliberate behavior change)

`OpenTab(std::optional<NavigationLocation> = NavigationLocation::Home())`. Ctrl+T and the chrome `+` therefore open a fresh Home tab instead of cloning the active path. This is a user-visible change from current behavior; it is intentional and documented here per the mission: "NEW TAB: Home... unless there is a strong existing architectural/user-behavior reason to keep clone active location." The existing navigation-and-workspace design already states "Opening a tab = empty history" (fresh-start semantics), and Home is the natural fresh start. Users who want the old behavior can use Ctrl+T then navigate; no migration of stored state is affected (tabs are not persisted). The sidebar middle-click "open in new tab" paths still pass explicit destinations and are unchanged. If this proves contentious in review, the fallback is a separate `navigation.home-tab` command — noted, not built.

### D8: Data freshness without polling

`WindowShell` adds `RefreshHomeData()` (rebuild `HomeData`, `columnView_.SetHomeData`, `RequestRepaint`), called from every existing refresh point that already reacts to the relevant change: after `NavigateWorkspace`/`NavigateColumnViewTo`/back/forward/tab/pane switches (where `navigationChrome_.Refresh()` is already called), after bookmark mutations (`navigation.add-bookmark`, sidebar context-menu rename/remove/move — the sidebar already calls `FlushState`+`Refresh` there; `RefreshHomeData` piggybacks), after `ClearRecentFolders` (Home clear button), after search execute/clear (SearchPanel callbacks), and after `ReResolveKnownFolders` (`WM_SETTINGCHANGE`). No timers, no polling; the existing 500 ms state-flush timer persists recents.

### D9: Keyboard model for the Home pane

Home inherits the pane's existing keyboard pipeline (`WindowShell` `WM_KEYDOWN` → `ColumnView::OnKeyDown`). In Home mode: Up/Down move the focused item across the flat item list (wrapping at section boundaries by simple clamping), Left/Right move between the two columns when a two-column layout is active, Enter activates (folder → navigate, search → execute, action → command), `Ctrl+Enter` activates in a new tab (folder/search only), Home/End jump to first/last item of the current section (or the whole list — pick whole list; simpler and predictable), Escape does nothing in the pane (shell already handles Escape for search/cancel per existing conventions). Focus is drawn with the `focusStroke` outline on the focused item; selection semantics (click-select vs hover) follow column conventions. Tab/Shift+Tab continue to traverse native chrome controls per the existing focus model (Home is not a WS_TABSTOP child; its keyboard lives on the shell window like ColumnView's).

### D10: Search integration reuses existing history and pipeline

`SearchPanel` gains three small accessors: `std::vector<std::wstring> RecentQueries(size_t maxCount) const` (reads `historyStore_.Queries()`, honoring the existing policy — if recording is disabled, stored entries still display per the existing spec), `void ExecuteQuery(const std::wstring& query)` (ShowAndFocus-equivalent + set query text + `Dispatch(recordHistory = true)`), and the existing `ClearHistory()` is exposed for the Home "Clear recent searches" action. No new parser, no new storage, no new retention semantics.

### D11: Degraded mode and privilege independence

Home has no service dependency by construction: Pinned/Recents/Searches/Known folders are all local UI state; navigation from Home goes through the normal degraded browse path; nothing is gated on `EngineConnectionState`, MFT snapshot, USN, or Administrator. `HomeData` builds and renders identically in every engine state.

## Risks / Trade-offs

- [Changing Ctrl+T default from clone-active-path to Home] → Mitigation: deliberate, documented (D7); tabs are not persisted so no state migration; behavior is trivially reversible by navigating after the tab opens.
- [Home's `currentPath == ""` collides with the "This PC" sentinel in consumers that ignore the kind flag] → Mitigation: every new kind-aware consumer checks `isHomeLocation` first; existing empty-path consumers behave identically to This PC (address bar seeds empty, file ops guard empty); regression tests cover the empty-path/kind matrix.
- [`Navigate` reconciliation (mouse-down/key-down) could record spurious recents] → Mitigation: recording only happens when the model location actually changes to a different filesystem path (Navigate already no-ops on identical paths); selection/scroll/Home never call it.
- [Recent-folder dedup by lexical identity may miss or conflate paths that differ only in casing/`\\?\` spelling] → Mitigation: documented equality policy (D3); persisted entries keep first-seen spelling; no filesystem resolution is attempted (which would be slow and would block on dead paths).
- [Two-column Home layout could overlap on odd widths] → Mitigation: layout is a pure function with a no-overlap contract tested headlessly across widths, DPI scales, and section counts.
- [Dual-pane Home (pane 1 Home, pane 2 FS) interactions with view-state save/restore] → Mitigation: `SavePaneState` skips Home panes; restore treats Home contexts as empty-column re-navigation; headless workspace tests cover pane isolation.
- [Search history grows unbounded (no length limit today — known deferred gap)] → Mitigation: Home displays a bounded subset (`RecentQueries(10)`); the underlying history cap remains the existing `SearchHistory` behavior (100).

## Migration Plan

- **On-disk:** additive `"recentFolders"` array in `workspace-state.json`; `version` stays `1`; old files load with an empty list; no rollback needed (absence of the field is the pre-feature behavior).
- **Code:** pure additions plus the documented `OpenTab` default change; all new APIs are additive; existing tests must stay green before and after each wave.
- **Rollback:** revert commits; old binaries ignore the new field and continue to clone-path tabs.

## Open Questions

None — deferred unknowns (visual polish details, exact empty-state copy) are safe to settle during implementation without changing specs, approach, or tasks.
