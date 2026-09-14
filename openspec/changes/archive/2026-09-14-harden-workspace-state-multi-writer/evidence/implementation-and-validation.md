# Implementation and validation — harden-workspace-state-multi-writer

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- `NavigationWorkspace::LoadState` now returns `WorkspaceLoadStatus`
  (`Ok` / `Missing` / `Failed`); the constructor records the status and the raw
  document. A read/decode/unsupported-version failure leaves the in-memory
  defaults but does not license a clobbering write (design D1).
- `FlushState` does not mark dirty for a failed load's last-location sync and
  no-ops entirely until the user mutates persistent state in this session
  (`AddBookmark`/`Rename`/`Reorder`/`Remove`, tab close/reopen,
  `MarkStateDirty` collapse toggles). Automatic recent-folder recording
  (`Navigate`) no longer counts as a user mutation.
- Writes use a unique temporary name (`workspace-state.json.<pid>.<tick>.tmp`)
  before `MoveFileExW` replace (design D2).
- On a dirty flush, the current document is re-read; when it changed since load
  (another live window wrote it), `MergeWorkspaceStateForWrite` unions
  bookmarks (case-insensitive path, writer's display name wins),
  `recentFolders` (existing `RecentFolderHistory` dedup/cap) and
  `recentlyClosedTabs` (typed equality, cap 16), with the writer's scalars
  last-writer-win. Bookmarks removed in this session are tombstoned and are not
  resurrected from the other writer's stale copy (design D3).

## Files

- `src/ui/src/NavigationWorkspace.{h,cpp}`
- `tests/navigation/test_navigation_workspace.cpp`

## Tests added

- An undecodable document plus a debounced flush with no user mutation leaves
  the original file bytes intact; a subsequent real bookmark mutation writes a
  recovery document.
- Two sequential writers (simulated live windows) union their bookmarks.
- A bookmark removed in one window is not resurrected by its flush.
- `recentFolders` union across writers without duplicates.
- Existing load-status assertions updated to `Ok`/`Failed` per case.

## Validation

- `ctest --test-dir build/debug -R ffnavigation --output-on-failure`:
  `ffnavigation_tests` PASS.
- `ffdoc_drift_tests` PASS after line-ending normalization.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
