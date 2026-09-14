## Context

See proposal.md. `NavigationWorkspace::LoadState` zeros `state`, returns true on missing file (OK), **and** on UTF-8 failure / unsupported version (not OK if FlushState follows). `SaveState` uses `file + L".tmp"` + `MoveFileExW`. No single-instance. `quick-access-home` already stores `recentFolders` in this document; merge must keep that field additive.

## Goals / Non-Goals

**Goals:** Failed load does not mark dirty; unique temp names; merge list fields across processes.

**Non-Goals:** Single-instance mutex as the primary fix (optional later). SQLite. Version bump unless merge absolutely needs it (prefer version 1).

## Decisions

### D1: Load failure does not set `stateDirty_`

Distinguish `LoadStatus { Ok, Missing, Failed }`. Missing → defaults, not dirty. Failed → defaults in memory, `loadFailed_` true, FlushState no-ops until a user mutation; user mutation writes a new document (D3 merge still attempts to re-read the file first).

**Why:** Today's `return true` with empty state plus the 500 ms flush timer is what wipes a good file.

### D2: Unique temporary file per write

`workspace-state.<pid>.<tick>.tmp` or `GetTempFileNameW` in the same directory, then `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.

**Why:** Two processes sharing `.tmp` can interleave truncates. Unique names are the cheap half of PA-012.

### D3: Reload-merge-write for list fields

On FlushState of list-bearing fields: re-read current file (if it parses); union bookmarks by path (case-insensitive); union recentFolders with existing cap/dedup (`RecentFolderHistory`); union recentlyClosedTabs with cap. Scalars (collapsed flags, lastLocation) last-writer-win from *this* process.

**Why:** Two windows adding different bookmarks is the user-visible bug. Last-writer-wins on sidebar collapsed is acceptable.

**Rejected:** Single-instance only — the product currently allows two processes (`wWinMain` has no mutex) and D2 of the foundation explicitly wanted multiple windows. Merge preserves that. File locking with no merge still last-writer-wins the whole document.

## Risks / Trade-offs

- **[Risk] Merge can resurrect a bookmark the user just deleted in the other window.** → Mitigation: acceptable for v1; deletion is last-writer-wins if we merge by "this process's list is authoritative for paths it still contains" — **prefer union of adds, and treat this process's removal as authoritative for paths it loaded and then removed** (tombstone in-memory for the session). Implement session tombstones for removed bookmark paths so a concurrent add of a *different* path still unions.
- **[Risk] Partial JSON parser is hand-rolled.** → Mitigation: do not expand parser scope; failed re-read on flush skips merge and writes this process's state only if `stateDirty_` from a user mutation (D1).

## Migration Plan

Version stays 1. Old files load. No rollback beyond reverting the binary.

## Open Questions

None.
