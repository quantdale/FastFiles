## Purpose

Home / Quick Access is a first-class, local navigation surface that gives FastFiles a fast starting point for new windows and tabs: pinned bookmarks, recently visited folders, recent searches, and common known folders, all fully usable in degraded mode with no privileged-service dependency.

## ADDED Requirements

### Requirement: Home is a typed navigation location, never a fake filesystem path
FastFiles SHALL represent Home as an explicit navigation-location kind distinct from filesystem paths. A Home location SHALL NOT be representable as, or convertible into, a filesystem path string, and SHALL NEVER be passed to Win32 filesystem APIs. The existing "This PC" synthetic root (empty path) SHALL remain distinct from Home. A filesystem destination navigated from Home SHALL become a normal filesystem context, and Back from it SHALL return to Home.

#### Scenario: Home is distinct from This PC
- **WHEN** the active context is Home
- **THEN** its location kind is Home, its filesystem path is empty, and it is never treated as the "This PC" root

#### Scenario: Navigating from Home to a folder
- **WHEN** the user activates a folder while Home is active
- **THEN** the context becomes a filesystem context at that folder and Back returns to Home

#### Scenario: Home never reaches filesystem APIs
- **WHEN** any code path resolves the active context's location for a filesystem operation
- **THEN** a Home location yields no path argument and the operation is rejected by existing empty-path guards

### Requirement: Persistent Home entry in the navigation sidebar
FastFiles SHALL show a persistent Home entry at the top of the navigation sidebar, above Drives, Known Folders, and Bookmarks. Activating it SHALL make Home the active location of the active pane. It SHALL show a selected state while the active location is Home, and middle-click SHALL open Home in a new tab, matching existing sidebar conventions.

#### Scenario: Clicking the sidebar Home entry
- **WHEN** the user clicks the sidebar Home entry
- **THEN** the active pane navigates to Home and the entry shows the selected state

#### Scenario: Middle-clicking the sidebar Home entry
- **WHEN** the user middle-clicks the sidebar Home entry
- **THEN** a new tab opens at Home and the current tab is unchanged

### Requirement: New tabs open at Home
FastFiles SHALL open new tabs at Home. The previous behavior of cloning the active tab's path is superseded; this change is documented in the change design.

#### Scenario: Ctrl+T opens Home
- **WHEN** the user presses Ctrl+T (or clicks the chrome new-tab button)
- **THEN** a new tab opens with Home as its location and an empty back/forward history beyond Home

### Requirement: Home shows pinned bookmarks
Home SHALL display the user's existing bookmarks as its Pinned section, reading the same bookmark store the sidebar uses. Each entry SHALL show a folder icon, a display name, and its path as secondary text where space allows. Activating an entry SHALL navigate the active context to that folder; a secondary action SHALL open it in a new tab using existing conventions. When there are no bookmarks, Home SHALL show an empty-state message inviting the user to pin folders.

#### Scenario: Pinned section mirrors bookmarks
- **WHEN** the user adds or removes a bookmark
- **THEN** Home's Pinned section reflects the change without an application restart

#### Scenario: Pinned item activates navigation
- **WHEN** the user activates a Pinned entry
- **THEN** the active context navigates to that folder

#### Scenario: Empty pinned state
- **WHEN** the user has no bookmarks
- **THEN** Home shows an empty-state message for the Pinned section

### Requirement: Home shows a bounded recent-folders history
FastFiles SHALL maintain a local, bounded, persisted history of recently visited folders, distinct from recently closed tabs. Filesystem folder navigation SHALL contribute an entry; selection-only changes, scrolling, and opening Home SHALL NOT. Repeated visits SHALL deduplicate, moving the folder to the front. History SHALL be most-recently-first, persisted with a bound of 50 entries, and Home SHALL display at most 10. Deduplication SHALL use a lexical identity policy (case-insensitive, trailing-separator-normalized) that never resolves the filesystem. Home SHALL provide a Clear action that removes all entries, persists immediately, updates Home immediately, and never deletes folders, bookmarks, or search history.

#### Scenario: Visiting a folder records it
- **WHEN** the user navigates to a filesystem folder
- **THEN** the folder appears at the front of Recent folders on Home

#### Scenario: Repeated visits deduplicate
- **WHEN** the user visits a folder already in the history
- **THEN** it moves to the front and no duplicate entry appears

#### Scenario: Lexical deduplication
- **WHEN** the user visits `C:\Folder`, then `c:\folder\`
- **THEN** the history contains one entry

#### Scenario: Selection and Home do not record
- **WHEN** the user changes selection, scrolls, or opens Home
- **THEN** no recent-folder entry is added

#### Scenario: Bounded persistence
- **WHEN** more than 50 folders are visited
- **THEN** only the 50 most recent remain persisted and Home shows at most the 10 most recent

#### Scenario: Clearing recent folders
- **WHEN** the user activates Clear in the Recent folders section
- **THEN** the list empties on Home immediately, the empty state persists, and no folders, bookmarks, or search history are deleted

### Requirement: Recent-folders persistence is migration-safe
The recent-folders history SHALL be stored in the existing per-user workspace-state JSON as an additive, optional field. Workspace files written before this feature SHALL load with an empty history. A malformed individual entry SHALL NOT fail the whole workspace load. Corrupted documents SHALL continue to follow the existing graceful fallback. Writes SHALL use the existing atomic, debounced, shutdown-flushed persistence machinery.

#### Scenario: Older workspace files load
- **WHEN** a workspace-state file without the recent-folders field is loaded
- **THEN** the application starts normally with an empty recent-folders history

#### Scenario: Malformed entry is skipped
- **WHEN** a workspace-state file contains a malformed recent-folder entry
- **THEN** that entry is skipped and the rest of the workspace loads normally

### Requirement: Home shows recent searches from the existing search history
Home SHALL display a bounded subset of the most recent queries from the existing search-history store — never a duplicate store. Activating a recent search SHALL open the Search surface, restore that query, and execute it through the existing search pipeline with the currently valid scope. A Clear recent searches action SHALL invoke the existing history-clearing operation. If search-history recording is disabled, Home SHALL follow the existing search-history policy (previously recorded entries remain available until cleared).

#### Scenario: Recent searches appear on Home
- **WHEN** the user has previously executed searches
- **THEN** Home lists the most recent queries from the existing search history

#### Scenario: Activating a recent search
- **WHEN** the user activates a recent-search entry
- **THEN** the Search surface opens with that query and executes it through the existing pipeline

#### Scenario: Clearing recent searches
- **WHEN** the user activates Clear recent searches
- **THEN** the existing search-history clearing operation runs and Home updates immediately

### Requirement: Home shows common known folders
Home SHALL display the existing known-folder discovery results (Desktop, Documents, Downloads, Pictures, Music, Videos) as its Common locations section, reusing the same discovery, redirection, and unavailability handling as the sidebar. Unavailable folders SHALL be shown disabled or omitted consistently with the sidebar.

#### Scenario: Known folders reuse existing discovery
- **WHEN** Home renders Common locations
- **THEN** it uses the same `SHGetKnownFolderPath`-based discovery as the sidebar, honoring redirection, with no hardcoded user paths

#### Scenario: Unavailable known folder
- **WHEN** a known folder cannot be resolved
- **THEN** it is rendered disabled or omitted per the existing sidebar behavior

### Requirement: Quick actions reuse existing commands
Home SHALL expose a small actions strip of at most five entries (New Tab, Search, Open Path, Settings), each invoking an existing command. No new behavior SHALL be added solely to populate Home.

#### Scenario: Quick action invokes existing command
- **WHEN** the user activates a quick action
- **THEN** the corresponding existing command runs

### Requirement: Home is keyboard accessible
The Home surface SHALL be fully usable without a mouse: arrow keys move focus across Home items, Enter activates the focused item, Ctrl+Enter opens a folder or search in a new tab, Home/End jump within the item list, and the focused item SHALL have a visible focus treatment using the existing focus-stroke token (suppressed in High Contrast). Escape SHALL behave per existing shell conventions. No global shortcuts SHALL be hijacked.

#### Scenario: Keyboard traversal of Home
- **WHEN** the user focuses the Home pane and presses arrow keys
- **THEN** focus moves between Home items with a visible focus outline

#### Scenario: Enter activates the focused item
- **WHEN** the user presses Enter on a focused Home item
- **THEN** the item activates (navigate, execute search, or run command)

### Requirement: Home supports dual-pane isolation
Home SHALL belong to the active pane/context. One pane MAY show Home while the other shows a filesystem location, and activating an item from Home SHALL change only that pane. Switching the active pane SHALL preserve each pane's independent location and history. Home in one tab SHALL NOT affect another tab. Dual-pane enable/disable SHALL remain correct with Home panes.

#### Scenario: Home in one pane only
- **WHEN** dual-pane mode is on and pane 1 is Home
- **THEN** pane 2 continues to show its filesystem location and its own history, and activating a Home item changes only pane 1

#### Scenario: Tab isolation
- **WHEN** the user switches tabs
- **THEN** each tab retains its own Home/back/forward state

### Requirement: Home is fully usable in degraded mode
Home SHALL operate without a privileged service connection, MFT snapshot, USN journal, or Administrator. Pinned folders, recent folders, recent searches, and known folders SHALL all be available, and navigation from Home SHALL fall through the normal unprivileged enumeration path.

#### Scenario: Home with no service
- **WHEN** the privileged service is absent, the handshake fails, or pins are placeholders
- **THEN** Home renders all sections from local state and navigation from Home works through degraded enumeration

### Requirement: Home updates without restart or polling
Home SHALL refresh its data when bookmarks change, a folder is visited, recent folders are cleared, a search executes, search history is cleared, or known folders are re-resolved. Home SHALL NOT use an idle polling loop.

#### Scenario: Live bookmark update
- **WHEN** the user adds a bookmark via Ctrl+D
- **THEN** the Home Pinned section updates immediately

### Requirement: Home is a responsive native surface
Home SHALL lay out through a headless layout seam that takes viewport size, DPI scale, and section counts and returns non-overlapping section/item rectangles, a column count, and clipping bounds. Narrow widths SHALL use a one-column list, normal widths one or two columns, and wide widths a bounded content width rather than stretching items across the viewport. Zero-item sections SHALL render without items. Home SHALL use the existing theme tokens, IconCache, High-Contrast behavior, and animation gate, and SHALL NOT use idle animation timers or arbitrary colors.

#### Scenario: Narrow layout
- **WHEN** the Home pane is narrow
- **THEN** items render in a single column with no overlap

#### Scenario: Wide layout is bounded
- **WHEN** the Home pane is very wide
- **THEN** content width is bounded and items do not stretch across the full viewport

#### Scenario: High Contrast
- **WHEN** Windows High Contrast is active
- **THEN** Home uses system colors and suppresses overlay/focus treatments per existing conventions

#### Scenario: DPI scaling
- **WHEN** the display DPI changes between 100% and 150%
- **THEN** Home layout scales with the existing DIP-based metrics and remains non-overlapping

### Requirement: Home performance is instant
Rendering Home SHALL NOT perform network access, recursive filesystem enumeration, synchronous icon crawling, or per-frame disk reads. Item counts SHALL be bounded (at most 10 recent folders, a bounded subset of recent searches, and the existing bookmark/known-folder lists). Persistence SHALL be read once at load time and writes SHALL ride the existing debounced flush.

#### Scenario: Home renders from local state only
- **WHEN** Home paints
- **THEN** it draws only from in-memory state and cached icons, with work proportional to the small number of visible entries
