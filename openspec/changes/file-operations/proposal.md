## Why

A file manager's core job is manipulating files reliably. Copy, move, and delete must not freeze the interface, must integrate with the Recycle Bin rather than destroy data by default, and must never silently overwrite something the user didn't intend to overwrite.

## What Changes

- Implement copy/cut/paste/move/rename/delete via the `IFileOperation` COM API, off the UI thread, with progress information (current file, overall progress, transfer speed, estimated time remaining where reliable) and cancellation where Windows permits it.
- Implement Recycle Bin integration as the default delete path; permanent deletion is a distinct, explicitly confirmed action, never the default.
- Implement conflict resolution when a destination already contains an item with the same name: replace, skip, automatically rename, or apply the chosen action to all remaining conflicts.
- Implement multi-selection using standard Windows patterns (Ctrl-click, Shift-click, Ctrl+A) and drag-and-drop, including interoperating correctly with real Explorer windows (both directions).
- Implement folder and file creation.
- Implement an operation history covering only genuinely reversible operations (rename, move, Recycle-Bin delete with restore) — explicitly not permanent delete or overwrite, since those are not reversible.
- Implement clear, non-crashing error handling for locked files, permission errors, and files or folders that disappear between being listed and being acted on.

## Capabilities

### New Capabilities
- `file-operations-core`: Copy/move/delete/rename/create via `IFileOperation`, Recycle Bin integration, progress/cancel, and the reversible-operation history.
- `conflict-resolution`: The replace/skip/rename/apply-to-all-remaining decision flow for destination naming conflicts.
- `multi-selection-and-dragdrop`: The selection model (Ctrl-click/Shift-click/Ctrl+A) and drag-and-drop, including Explorer interoperability.

### Modified Capabilities
(none)

## Impact

- Invoked from `column-view-browsing`, and later `navigation-and-workspace` and `storage-analysis`, as the surfaces operations are initiated from — this change adds no navigation UI of its own.
- Operates directly against the filesystem via Win32/COM; independent of the index (`filesystem-index-store`), though a completed operation should invalidate/update any cached index entries for the affected paths.
