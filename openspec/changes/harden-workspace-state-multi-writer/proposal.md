## Why

Bookmarks, sidebar collapse, last location, recently-closed tabs, and recent folders all persist in one `%LOCALAPPDATA%\FastFiles\workspace-state.json`. `bookmarks-and-sidebar` requires that list to survive restart. The UI has no single-instance guard (`wWinMain`), each process loads once and `SaveState` overwrites the whole document via a **shared** `file + L".tmp"` name (PA-012). A UTF-8 decode or unsupported-version failure in `LoadState` returns **true** with an empty `WorkspaceState`; the next `FlushState` then persists empty as truth and destroys bookmarks. Two concurrent FastFiles windows last-writer-win each other's pins/recents.

## What Changes

- Treat load failure (unreadable, undecodable, unsupported version) as **do-not-clobber**: the process still launches with defaults, but SHALL NOT flush those defaults over a file it failed to parse until the user actually mutates state in this session *and* the write is a merge or a documented recovery.
- Concurrent writers SHALL NOT share a fixed `.tmp` path (unique temporary name) and SHALL NOT silently replace another process's bookmarks/recents. Preferred approach: reload-merge-write for list fields (bookmarks, recent folders, recently-closed tabs) with last-writer-wins only for scalar UI flags (sidebar collapsed).
- Optional but in-scope if cheap: a same-user single-instance or "second window is another process of the same app" warning is **not** required; correct merge is.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `bookmarks-and-sidebar`: persistence SHALL survive a second live window and SHALL NOT replace a well-formed on-disk document with empty defaults after a failed load.

## Impact

- `src/ui/src/NavigationWorkspace.{h,cpp}`, possibly `Main.cpp` only if a mutex/instance helper is chosen (design prefers merge over single-instance).
- Tests: `tests/navigation/test_navigation_workspace.cpp` today treats corrupt→empty as OK; add "failed load does not FlushState wipe" and two-writer merge cases.
- `quick-access-home` recentFolders stays an additive field in the same file; this change must not break that additive load.
- Non-goals: new database; schema version bump unless merge requires a version 2 (prefer staying at version 1).

## Open Questions

- None. Merge list fields; unique temp file; failed load does not mark dirty.
