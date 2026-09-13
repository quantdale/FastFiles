## Why

Paste after Copy/Cut reads `CF_HDROP` and `Preferred DropEffect` through `ReadFilesFromClipboard`. The write path correctly `GlobalLock`s the `HGLOBAL` (`CopyFilesToClipboard`); the read path calls `GetClipboardData` and treats the return as a usable `HDROP` / `DWORD*` without locking (PA-015). Win32 documents that `GetClipboardData` returns a handle that must be locked before dereference. The OLE drop-target extractor in `OleDragDrop.cpp` already locks — clipboard read is the outlier. Delay-rendered or serialized clipboard providers can yield garbage paths or an access violation on paste.

## What Changes

- `ReadFilesFromClipboard` (and `HasFilesOnClipboard` if it similarly peeks the handle) SHALL `GlobalLock`/`GlobalUnlock` every `HGLOBAL` obtained from `GetClipboardData` before reading bytes, matching the write path and the OLE extractor.
- Failed lock SHALL return an empty path list (paste no-ops) rather than dereferencing an unlocked handle.
- Add a unit/regression test that round-trips an HDROP through the real clipboard APIs with lock, and a structural test that the read function's lock pairing cannot regress.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `context-menus-and-quick-actions`: clipboard file-list paste SHALL read `CF_HDROP` through a locked `HGLOBAL`.

## Impact

- `src/ui/src/QuickActions.{h,cpp}`; callers in `WindowShell.cpp` paste path remain unchanged.
- Tests: extend `tests/ui/test_quick_actions.cpp` (today only covers `CanUseLocalCutClipboardFallback`).
- Non-goals: OLE drag-out MOVE (separate); changing Preferred DropEffect semantics.

## Open Questions

- None.
