## Why

FastFiles always drops the user into an arbitrary filesystem location (last session, startup setting, or the synthetic "This PC" root). There is no natural landing surface for "where do I want to go?" — no pinned folders, no recent folders, no recent searches surfaced anywhere outside the sidebar/search panel. Home / Quick Access makes FastFiles feel like a complete everyday file manager by giving every new window, new tab, and sidebar click a fast, local, native start surface.

## What Changes

- Add a typed **navigation-location kind** (`FileSystem` / `Home`) so Home is a first-class navigation state, never a fake filesystem path (`C:\Home`, `fastfiles://home`, or any magic string).
- Make Home the default start surface for **new tabs** (existing behavior clones the active path; this is a deliberate, documented change). There is no new-window command today, so nothing to change there.
- Add a persistent **Home entry at the top of the navigation sidebar**, clickable, with a visible selected state when Home is active, and middle-click-open-in-new-tab matching existing sidebar conventions.
- Render a **Home surface in the active pane** (inside the existing ColumnView surface, not a separate framework) with sections:
  - **Pinned** — the existing bookmarks list (no second favorites database).
  - **Recent folders** — a new bounded, persisted, deduplicated history of visited folders, stored in the existing per-user workspace-state JSON.
  - **Recent searches** — the existing `ffsearch` search history (no duplicate storage).
  - **Common locations** — the existing known-folder discovery (Desktop, Documents, Downloads, Pictures, Music, Videos).
  - **Quick actions** — a small strip (New Tab, Search, Open Path, Settings) reusing existing commands only.
- Add clear/removal behavior: recent folders have a Clear action; recent searches reuse the existing Clear History operation; Pinned exposes no bulk-clear (bookmarks already have per-item remove).
- Add a headless responsive-layout helper (`HomeLayout`) with headless tests, keyboard traversal for the Home surface, dual-pane isolation, and full degraded-mode support (Home has zero dependency on the privileged service, MFT/USN, or Administrator).
- Keep everything local and private: no telemetry, no network, no cloud; history is bounded and clearable.

## Capabilities

### New Capabilities
- `quick-access-home`: The Home navigation location, its sidebar entry, its sections (pinned bookmarks, recent folders, recent searches, common locations, quick actions), persistence/bounds/dedup of recent folders, keyboard/dual-pane/degraded-mode behavior, and the responsive layout contract.

### Modified Capabilities
- (none — the existing `bookmarks-and-sidebar`, `tabs`, `address-bar-and-history`, `search-ui`, and `theming` specs describe behavior that is extended additively; their requirements do not change. The new capability's spec captures the additive behavior.)

## Impact

- **Code (UI target `FastFiles`, `src/ui/`):**
  - `NavigationWorkspace.{h,cpp}` — typed location in `NavigationContext` (kind + typed history), `Navigate` overload accepting the location kind, `OpenTab` defaulting to Home, recent-folders recording/clear/accessors, workspace-state JSON field `recentFolders` with migration-safe loading.
  - New `NavigationLocation` header — the typed location model (kind enum + path, with a Home factory).
  - New headless `HomeLayout` helper (`src/ui/include/ffui/`) — pure layout math for the Home surface, mirroring the existing `ShellLayout`/`DetailsLayout` pattern.
  - `ColumnView.{h,cpp}` — per-pane Home rendering mode (sections, cards/rows, focus, keyboard, hit-testing) fed by a `HomeData` snapshot and an activation callback; no engine request is issued for Home.
  - `NavigationSidebar.{h,cpp}` — persistent Home row at top, typed `NavigationTarget`, selected-state wiring.
  - `NavigationChrome.{h,cpp}` — Home breadcrumb segment, tab label for Home tabs.
  - `WindowShell.{h,cpp}` — Home activation, `navigation.home` command (Alt+Home), new-tab default to Home, HomeData freshness wiring (bookmark/recent/search changes), details-pane gating on Home.
  - `SearchPanel.{h,cpp}` — small accessors: recent queries list, execute-a-query, clear-history passthrough (no new persistence).
  - `CommandSystem.cpp` — `navigation.home` command registration.
- **Tests:** new headless tests under `tests/navigation/` (location model, recent folders, workspace Home navigation) and `tests/ui/` (Home layout); existing suites must stay green.
- **Persistence:** additive optional field in the existing `%LOCALAPPDATA%\FastFiles\workspace-state.json`; no schema version bump; old files load unchanged.
- **Non-goals:** no new persistence system, no SQLite changes, no third-party dependencies, no privileged/service changes, no telemetry, no network access, no new fonts/icons.
