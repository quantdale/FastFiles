## 1. Navigation Context Model

- [ ] 1.1 Define the `NavigationContext` struct (current path, per-column selection/scroll state, back/forward history stack, address bar mode) as a plain in-process object with no owned IPC/engine handle
- [ ] 1.2 Extend the existing single per-process `EngineConnection` (from `establish-architecture-foundation`) with an API surface for "request/subscribe to a directory listing at path P," usable concurrently by any number of `NavigationContext` instances
- [ ] 1.3 Implement instantiation of a `column-view-browsing` instance bound to a given `NavigationContext`, reading only through the shared `EngineConnection`
- [ ] 1.4 Verify two or more concurrently live `NavigationContext` instances reading different paths produce correct, independent results with zero additional IPC round-trips beyond the shared snapshot's existing notification mechanism

## 2. Address Bar — Breadcrumb and Editable-Text Modes

- [ ] 2.1 Implement breadcrumb rendering: one clickable segment per path component, regenerated whenever the bound `NavigationContext`'s current path changes
- [ ] 2.2 Implement breadcrumb segment click handling: navigate the bound context to the clicked ancestor path
- [ ] 2.3 Implement the breadcrumb-to-editable-text mode switch (trailing-space click, dedicated toggle affordance, keyboard shortcut) with the current path pre-filled and selected
- [ ] 2.4 Implement editable-text-to-breadcrumb revert on Escape or focus loss without commit, discarding uncommitted typed text
- [ ] 2.5 Implement the path-parsing pipeline: trim/dequote, trailing-separator normalization, forward-to-back slash normalization, `%VAR%` environment-variable expansion, relative-segment canonicalization against an absolute root, explicit rejection of inputs with no absolute drive-letter or UNC root
- [ ] 2.6 Implement the three-way outcome on commit: syntactically invalid (inline error, no navigation), well-formed-but-nonexistent (inline error, no navigation), well-formed-and-exists-but-inaccessible (navigate; rely on destination in-column permission-denied state)
- [ ] 2.7 Unit tests for the parsing pipeline: quoted paths, trailing slashes/backslashes, forward slashes, environment variables, UNC paths, embedded `..` segments, bare relative paths, reserved characters, empty/whitespace-only input

## 3. Back/Forward History

- [ ] 3.1 Implement history recording keyed to folder-change navigation events only (descend, breadcrumb click, committed path entry, sidebar click, drive selection) — explicitly excluding selection-only and scroll-only changes
- [ ] 3.2 Implement Back/Forward stack semantics: moving the pointer without pushing a new entry, and truncating stale forward entries after a new navigation following a Back
- [ ] 3.3 Wire history state and Back/Forward affordances to a specific `NavigationContext` instance, confirming no shared or cross-context history state exists
- [ ] 3.4 Implement Back/Forward UI affordances (buttons and keyboard shortcuts) with disabled state when no history is available in that direction

## 4. Drive Selection

- [ ] 4.1 Implement drive/volume enumeration for the drive-selection control (via the engine if available, else directly via `GetLogicalDrives`)
- [ ] 4.2 Implement drive selection navigating the active navigation context to the selected drive's root path
- [ ] 4.3 Implement graceful handling of selecting an enumerated-but-currently-unreadable drive, routing through the standard nonexistent/inaccessible-path feedback

## 5. Tabs

- [ ] 5.1 Implement a tab strip UI component managing an ordered collection of `NavigationContext` instances, one active at a time
- [ ] 5.2 Implement "open new tab," initializing a fresh `NavigationContext` with empty history and either a default path or a clone of the active tab's current path
- [ ] 5.3 Implement "close tab," discarding that tab's live `NavigationContext` state and preventing closing the sole remaining tab (or auto-opening a replacement default tab)
- [ ] 5.4 Implement "switch active tab," changing only which `NavigationContext`'s surface is visible/focused with no mutation of any tab's state
- [ ] 5.5 Implement a bounded recently-closed-tabs record (path plus minimal metadata, most-recent-first) and "reopen closed tab," creating a new `NavigationContext` at the recorded path with fresh history
- [ ] 5.6 Implement keyboard shortcuts for new tab, close tab, next/previous tab, and reopen closed tab

## 6. Dual-Pane Mode

- [ ] 6.1 Implement the dual-pane split view: two `NavigationContext`-bound navigation surfaces (each with its own address bar) rendered side by side within the active tab's content area
- [ ] 6.2 Implement "enable dual-pane mode," cloning the active tab's current path into a new second-pane `NavigationContext` with fresh history
- [ ] 6.3 Implement "disable dual-pane mode," retaining the previously active pane's context as the tab's single context and discarding the other
- [ ] 6.4 Implement active-pane tracking (click-to-activate) and route sidebar-navigation clicks and keyboard focus to the active pane only
- [ ] 6.5 Confirm dual-pane state is scoped per-tab: enabling/disabling it in one tab must not affect any other open tab

## 7. Known-Folder Discovery

- [ ] 7.1 Implement known-folder enumeration via `SHGetKnownFolderPath`/`IKnownFolderManager` for Desktop, Documents, Downloads, Pictures, Videos, Music (and any additional registered known folders as a stretch item)
- [ ] 7.2 Implement resolution of known-folder redirection (relocated target paths) rather than assuming default paths
- [ ] 7.3 Implement re-resolution of known-folder paths on relevant system notification (e.g. a known-folder-path-change signal), refreshing the sidebar's Known Folders section
- [ ] 7.4 Implement graceful in-sidebar handling of a registered-but-unresolvable known folder (still listed, standard error feedback on click)

## 8. Bookmarks and Local Persistence

- [ ] 8.1 Define the local workspace-state file format (JSON) covering bookmarks (path, display name, sort order) and recently-closed tabs, stored under the per-user local app-data path
- [ ] 8.2 Implement load-at-startup with graceful fallback to empty/default state on a missing or corrupt file (never blocking application launch)
- [ ] 8.3 Implement debounced save-on-change plus flush-on-clean-shutdown
- [ ] 8.4 Implement "add bookmark" (from the current navigation context's path), rename, reorder (drag-and-drop or equivalent), and remove actions
- [ ] 8.5 Implement stale-bookmark handling: a bookmark whose path no longer resolves remains listed and routes through standard nonexistent/inaccessible-path feedback on click, rather than being auto-removed

## 9. Sidebar

- [ ] 9.1 Implement the single sidebar panel with three independently collapsible/expandable sections: Drives, Known Folders, Bookmarks
- [ ] 9.2 Implement overall sidebar collapse/expand (to minimal-width or hidden, with a re-expand affordance) that reclaims horizontal space for the navigation surface
- [ ] 9.3 Implement persistence of overall sidebar collapse state and each section's collapse state across restarts (extending the workspace-state file from section 8)
- [ ] 9.4 Implement click-to-navigate for Drives/Known-Folders/Bookmarks entries, targeting the active navigation context
- [ ] 9.5 Implement the secondary "open in new tab" action (e.g. middle-click) for sidebar entries

## 10. Integration and Validation

- [ ] 10.1 End-to-end test: two tabs with independent histories, back/forward in one does not affect the other
- [ ] 10.2 End-to-end test: dual-pane cross-location scenario used to stage a copy/move handoff to `file-operations` (verifying both panes remain independently navigable throughout)
- [ ] 10.3 End-to-end test: pasted path from an external source (e.g. clipboard text with quotes and a trailing backslash) navigates correctly
- [ ] 10.4 End-to-end test: application restart restores bookmarks, sidebar collapse state, and known-folder list correctly, and tolerates a deliberately corrupted workspace-state file without failing to launch
- [ ] 10.5 Manual accessibility/keyboard-navigation pass across address bar modes, tab strip, dual-pane pane switching, and sidebar
