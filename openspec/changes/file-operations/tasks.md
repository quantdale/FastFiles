## 1. Worker Thread & IFileOperation Infrastructure

- [ ] 1.1 Create the dedicated file-ops worker thread in `FastFiles`, initialized as `COINIT_APARTMENTTHREADED` (STA), with its own message loop
- [ ] 1.2 Implement the in-process request queue from the UI thread to the worker thread (post copy/move/rename/delete/create requests as plain-data messages, no raw COM pointers crossing threads)
- [ ] 1.3 Implement the `IFileOperationProgressSink` class used by all batch operations (`Pre*`/`Post*Item`, `UpdateProgress`), registered via `Advise` before `PerformOperations`
- [ ] 1.4 Implement the event-marshaling path from worker thread back to UI thread (progress, completion, per-item error, cancellation state) via window messages carrying plain data only
- [ ] 1.5 Implement serialized queuing so a second requested operation waits for the current one to finish on the same worker thread, with a visible "queued" vs. "in progress" UI state

## 2. Core File Operations

- [ ] 2.1 Implement Copy (`IFileOperation::CopyItems`) for single and multi-item selections
- [ ] 2.2 Implement Move (`IFileOperation::MoveItems`) for single and multi-item selections
- [ ] 2.3 Implement Rename (`IFileOperation::RenameItem`), including client-side validation of reserved characters and sibling-name collisions before submission
- [ ] 2.4 Implement New Folder / New File creation (`IFileOperation::NewItem`), with the created item immediately ready for inline rename
- [ ] 2.5 Wire copy/move/rename/create entry points into `column-view-browsing`'s context menu and standard keyboard shortcuts (Ctrl+C/X/V, F2, Ctrl+Shift+N, etc.)

## 3. Progress, Cancellation, and Error Handling

- [ ] 3.1 Implement the progress UI surface: current-item name, overall percentage, transfer speed, and ETA display
- [ ] 3.2 Implement the rolling-average speed/ETA calculation from `UpdateProgress` work-unit deltas, suppressing speed/ETA until enough samples exist
- [ ] 3.3 Implement the Cancel affordance and the cooperative cancellation path (stop signal honored between/mid items, accounting for completed vs. not-attempted items)
- [ ] 3.4 Implement per-item error capture in the progress sink (locked file, access denied, vanished source) without aborting the remaining batch
- [ ] 3.5 Implement the end-of-batch summary UI ("Copied N of M — K failed") with access to per-item failure detail
- [ ] 3.6 Verify no crash/hang paths exist for locked files, permission errors, and items that disappear between listing and processing

## 4. Recycle Bin and Permanent Delete

- [ ] 4.1 Implement default Delete (Del key, context menu) via `IFileOperation::DeleteItems` with the Recycle-Bin-enabled flag equivalent to `FOF_ALLOWUNDO`
- [ ] 4.2 Implement permanent delete as a distinct command (Shift+Delete, separate context-menu entry), invoking `DeleteItems` without the Recycle-Bin flag
- [ ] 4.3 Implement the permanent-delete confirmation dialog: explicit item count/names, "cannot be undone" copy, Cancel as default-focused action, and no suppress-this-dialog preference
- [ ] 4.4 Verify the default-delete confirmation preference (if/when suppressible) has no effect on the permanent-delete confirmation

## 5. Conflict Resolution

- [ ] 5.1 Implement the pre-flight destination-name collision check (`GetFileAttributesEx`-based) run before submitting a copy/move batch to `IFileOperation`
- [ ] 5.2 Implement the custom in-app conflict dialog (Replace / Skip / Keep Both + "Apply to all remaining" checkbox), styled consistent with FastFiles chrome
- [ ] 5.3 Implement the per-batch conflict state machine: pause at each conflicting item, resolve via dialog or stored apply-to-all choice, resume the batch with the resolved destination name or Skip
- [ ] 5.4 Implement Replace (proceed with original destination name, allow overwrite)
- [ ] 5.5 Implement Skip (omit the conflicting item from the batch entirely)
- [ ] 5.6 Implement Keep Both / automatic rename (generate a non-colliding destination name, e.g. "file (2).txt")
- [ ] 5.7 Implement "Apply to all remaining" persistence for the remainder of one batch only, reset for the next independently initiated operation
- [ ] 5.8 Verify a resolved conflict is never re-prompted within the same batch

## 6. Undo / Operation History

- [ ] 6.1 Implement the in-memory per-session operation history stack
- [ ] 6.2 Record completed rename operations (`{originalName, newName, parentPath}`) on successful `Post*Item` callback
- [ ] 6.3 Record completed move operations (`{sourcePath, destinationPath}` per item) on successful completion
- [ ] 6.4 Record completed Recycle-Bin deletes (`{originalPath, recycleBinItemIdentifier}` per item) on successful completion
- [ ] 6.5 Implement Undo for rename (rename back), move (move back via the normal tracked move path), and Recycle-Bin delete (restore from Recycle Bin to original location)
- [ ] 6.6 Structurally omit any history-push call site for permanent delete and for Replace-conflict overwrites — verify by code inspection that no path exists to record either, not merely that a flag defaults to off
- [ ] 6.7 Implement a clear, non-crashing "can no longer be undone" message if a Recycle-Bin restore fails (e.g., Recycle Bin was emptied out-of-band)
- [ ] 6.8 Verify undo history does not persist across a FastFiles restart

## 7. Multi-Selection Model

- [ ] 7.1 Implement per-pane selection state (selected set, anchor index, focus index) independent across panes
- [ ] 7.2 Implement plain click (clear-and-select-one)
- [ ] 7.3 Implement Ctrl-click toggle without disturbing other selected items
- [ ] 7.4 Implement Shift-click contiguous range selection from the current anchor, re-anchoring correctly on repeated Shift-clicks
- [ ] 7.5 Implement Ctrl+A scoped to the active pane's currently visible/listed items only
- [ ] 7.6 Verify selecting within a child column does not clear or alter the parent column's own selection highlight
- [ ] 7.7 Wire "current selection in the active pane" as the uniform input contract consumed by all file-operation entry points (context menu, keyboard shortcuts, drag start)

## 8. Native OLE Drag-and-Drop

- [ ] 8.1 Implement the `IDataObject` implementation exposing selected items as `CF_HDROP` (`DROPFILES` structure with real paths)
- [ ] 8.2 Implement `IDropSource` and wire drag initiation from a pane's selection (`DoDragDrop`)
- [ ] 8.3 Implement `IDropTarget` (`DragEnter`/`DragOver`/`DragLeave`/`Drop`) and `RegisterDragDrop` registration for each Column View pane HWND
- [ ] 8.4 Implement `CF_HDROP` extraction on drop, with fallback to the Shell ID List Array format when a source only offers that
- [ ] 8.5 Implement drag-effect negotiation in `DragOver` (`pdwEffect`): Ctrl = copy, Shift = move, Ctrl+Shift = link, no modifier = move within volume / copy across volumes
- [ ] 8.6 Route a completed drop through the same worker-thread copy/move path as menu/keyboard operations, including conflict resolution and undo-history recording
- [ ] 8.7 Test dragging out of FastFiles into a real Explorer window (both copy and move effects)
- [ ] 8.8 Test dragging from a real Explorer window into FastFiles
- [ ] 8.9 Test dragging between two FastFiles windows/panes

## 9. Index Invalidation Notification

- [ ] 9.1 Implement the best-effort notification from `FastFiles` to `FastFilesEngine` (over the existing engine↔UI control pipe) of paths affected by a completed operation
- [ ] 9.2 Verify a completed file operation is reported to the user as complete without waiting on or being blocked by this notification, including when `FastFilesEngine` is unreachable

## 10. Integration and Validation

- [ ] 10.1 End-to-end test: copy/move/rename/delete/create initiated from `column-view-browsing` context menu and keyboard shortcuts
- [ ] 10.2 End-to-end test: large batch (thousands of small files, and a few multi-gigabyte files) copy/move with live progress, speed/ETA, and successful cancellation mid-batch
- [ ] 10.3 End-to-end test: batch containing a locked file and a vanished file completes the rest of the batch and reports both failures clearly
- [ ] 10.4 End-to-end test: full conflict-resolution flow (Replace, Skip, Keep Both, Apply to all remaining) across a multi-item batch
- [ ] 10.5 End-to-end test: Undo of a move, a rename, and a Recycle-Bin delete; confirm permanent delete and overwrite never appear as undoable
- [ ] 10.6 End-to-end test: Ctrl-click, Shift-click, and Ctrl+A selection behavior within a single pane and across multiple panes
- [ ] 10.7 Adversarial/edge-case test: extremely long paths, reserved filenames, and cloud-placeholder (Files-On-Demand) items — verify graceful, non-crashing behavior even where full semantics are out of scope
