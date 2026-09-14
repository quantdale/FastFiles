## Why

`multi-selection-and-dragdrop` requires dragging a FastFiles selection into Explorer (or another OLE target) to copy or **move** according to standard Windows effect conventions. The current source path does not complete a MOVE: `WindowShell` discards `BeginFileDrag`'s `DoDragDrop` effect, `FileDataObject` offers only `CF_HDROP`, and there is no `CFSTR_PERFORMEDDROPEFFECT` / source-delete path (PA-014, still present). Same-volume default is MOVE (`DetermineDropEffect`), so a same-volume drag-out that the target reports as MOVE can leave duplicates at the source. Intra-app drops already go through `IFileOperation` and are out of scope except for same-basename link drops (PA-026).

## What Changes

- Honor the negotiated OLE drop effect after `DoDragDrop` returns. When the target reports MOVE (and has not itself deleted the sources via the HDROP paths), FastFiles SHALL complete the move by deleting the source items through the existing `file-operations-core` worker (Recycle-Bin default unless the user explicitly chose permanent delete — drag-out uses Recycle-Bin delete of sources that the target copied).
- Advertise and consume `CFSTR_PREFERREDDROPEFFECT` / `CFSTR_PERFORMEDDROPEFFECT` so FastFiles can distinguish "target already moved the files" from "target copied and expects the source to delete."
- Intra-batch link-drop name reservation: two same-basename sources SHALL NOT overwrite a single `.lnk` (PA-026).

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `multi-selection-and-dragdrop`: drag-out MOVE completion and performed-drop-effect handling; link-drop unique destination names.

## Impact

- `src/ui/src/OleDragDrop.{h,cpp}`, `WindowShell.cpp` (`BeginFileDrag` call site, link-drop planner).
- Tests: `tests/ui/test_drop_destination.cpp` / `tests/fileoperations/` — add effect-completion and two-source link-drop cases. No Explorer e2e required; a headless IDataObject/effect test plus a source-delete seam is sufficient.
- Non-goals: changing intra-app copy/move (already `QueueTransfer`); clipboard HDROP locking (separate change).

## Open Questions

- None. Use the standard OLE performed-drop-effect protocol rather than blindly deleting on every MOVE return (Explorer often moves HDROP paths itself).
