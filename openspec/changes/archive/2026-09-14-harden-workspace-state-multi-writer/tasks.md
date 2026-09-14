## 1. Load-failure must not clobber

- [x] 1.1 Distinguish LoadStatus Ok / Missing / Failed in `NavigationWorkspace::LoadState`. Failed decode or unsupported version SHALL NOT set `stateDirty_` (design D1).
- [x] 1.2 `FlushState` no-ops while `loadFailed_` is set and the user has not mutated workspace state this session.

## 2. Unique temp + merge

- [x] 2.1 Write through a unique temporary name in the workspace directory, then `MoveFileExW` replace (design D2). Stop using a shared `workspace-state.json.tmp`.
- [x] 2.2 On dirty flush, re-read a well-formed on-disk document and union bookmarks (by path), `recentFolders` (existing cap/dedup), and recently-closed tabs; last-writer-win scalars; session tombstones for bookmarks removed in this process (design D3).

## 3. Tests

- [x] 3.1 Failed UTF-8 load then FlushState without user mutation leaves the original bytes intact.
- [x] 3.2 Two sequential SaveState calls with different bookmark adds both appear after a merge (simulate two writers).
- [x] 3.3 `recentFolders` additive load from `quick-access-home` still works on version-1 files.
- [x] 3.4 Run `ctest --test-dir build/debug -R ffnavigation --output-on-failure`.
