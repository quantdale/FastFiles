## ADDED Requirements

### Requirement: Off-UI-Thread Operation Execution
`FastFiles` SHALL execute all copy, move, rename, delete, and create operations through `IFileOperation` on a dedicated worker thread initialized as a single-threaded apartment (STA), and SHALL NOT call `IFileOperation` methods on the UI thread. The UI thread SHALL remain responsive (able to repaint, accept input, and process other windows) for the entire duration of any batch operation, regardless of item count or size.

#### Scenario: Large copy does not freeze the window
- **WHEN** a user initiates a copy of a batch of items whose total size takes longer than one second to transfer
- **THEN** the FastFiles window SHALL continue to repaint, accept input, and respond to other user actions for the entire duration of the copy

#### Scenario: Multiple operations requested in sequence
- **WHEN** a user initiates a second operation while an earlier operation is still in progress
- **THEN** the second operation SHALL be queued and executed on the same worker thread without blocking the UI thread, and SHALL NOT be silently dropped

### Requirement: Copy Items
`FastFiles` SHALL copy one or more selected files or folders from a source location to a destination location via `IFileOperation::CopyItems`, preserving the source items unchanged.

#### Scenario: Copying a selection to a new folder
- **WHEN** a user copies a selection of files and folders into a destination folder that contains no conflicting names
- **THEN** the items SHALL appear at the destination with their original content and names, and the source items SHALL remain unchanged at their original location

### Requirement: Move Items
`FastFiles` SHALL move one or more selected files or folders from a source location to a destination location via `IFileOperation::MoveItems`, removing them from the source location once the move completes.

#### Scenario: Moving a selection to a new folder
- **WHEN** a user moves a selection of files and folders into a destination folder that contains no conflicting names
- **THEN** the items SHALL appear at the destination and SHALL NO LONGER appear at the original source location once the operation completes successfully

### Requirement: Rename Item
`FastFiles` SHALL rename a single selected file or folder in place via `IFileOperation::RenameItem`, validating the proposed name against reserved filesystem characters and existing sibling names before submitting the rename.

#### Scenario: Renaming to a valid, non-conflicting name
- **WHEN** a user renames a selected item to a name that contains no reserved characters and does not collide with an existing sibling
- **THEN** the item SHALL be renamed in place and continue to occupy the same position in its parent folder

#### Scenario: Renaming to a name containing reserved characters
- **WHEN** a user attempts to rename an item to a name containing a character the filesystem does not permit
- **THEN** FastFiles SHALL reject the rename before submitting it to `IFileOperation` and SHALL show a clear, non-crashing message explaining why

### Requirement: Create New File or Folder
`FastFiles` SHALL support creating a new empty folder or a new empty file within the currently active pane's location via `IFileOperation::NewItem`.

#### Scenario: Creating a new folder
- **WHEN** a user invokes "New Folder" within an active pane
- **THEN** a new, empty folder SHALL be created in that pane's current location with a default name ready for immediate rename, and SHALL appear in the pane's listing

### Requirement: Delete to Recycle Bin as the Default Deletion Path
`FastFiles` SHALL route the default delete action (Delete key, context-menu "Delete") through `IFileOperation` with the Recycle-Bin-enabled flag equivalent to `FOF_ALLOWUNDO`, so deleted items are recoverable via the Windows Recycle Bin rather than being permanently removed.

#### Scenario: Deleting a selection via the default Delete command
- **WHEN** a user selects one or more items and invokes the default Delete command
- **THEN** the items SHALL be moved to the Recycle Bin rather than permanently erased, and SHALL be restorable through normal Recycle Bin mechanisms

### Requirement: Permanent Deletion as a Distinct, Explicitly Confirmed Action
`FastFiles` SHALL provide permanent deletion (bypassing the Recycle Bin) only as a separate, explicitly labeled command distinct from the default Delete command, bound to Shift+Delete and a distinctly labeled context-menu entry. Permanent deletion SHALL require an explicit confirmation dialog that cannot be suppressed by any "don't ask again" preference, with its default focused action set to Cancel rather than the destructive action.

#### Scenario: Invoking permanent delete
- **WHEN** a user selects one or more items and invokes Shift+Delete or the "Delete permanently" context-menu entry
- **THEN** FastFiles SHALL show a confirmation dialog stating the action cannot be undone, with Cancel as the default-focused action, before performing any deletion

#### Scenario: Confirming permanent delete
- **WHEN** a user explicitly confirms the permanent-delete confirmation dialog
- **THEN** the selected items SHALL be permanently deleted via `IFileOperation` without routing through the Recycle Bin

#### Scenario: Permanent-delete confirmation cannot be disabled
- **WHEN** a user has previously suppressed confirmation dialogs for the default (Recycle-Bin) delete command
- **THEN** the permanent-delete confirmation dialog SHALL still be shown in full, unaffected by that preference

### Requirement: Batch Progress Reporting
`FastFiles` SHALL report progress for any in-flight copy, move, or delete batch operation, including the item currently being processed, overall completion percentage across the batch, and transfer speed and estimated time remaining when a reliable estimate is available.

#### Scenario: Progress updates during a multi-item operation
- **WHEN** a batch copy or move operation is in progress
- **THEN** the progress UI SHALL display the name of the item currently being processed and an overall completion percentage that advances as items complete

#### Scenario: Speed and ETA suppressed when not yet reliable
- **WHEN** a batch operation has been running for less than approximately one second or has completed too few work units to compute a stable rate
- **THEN** FastFiles SHALL withhold a numeric transfer speed and estimated time remaining rather than displaying an unreliable value

### Requirement: Operation Cancellation
`FastFiles` SHALL allow a user to cancel an in-progress copy, move, or delete batch operation. Cancellation SHALL be cooperative: items already completed at the time of cancellation SHALL remain completed and unaffected, and the operation SHALL report a clear "cancelled" state reflecting how many items completed versus were not attempted.

#### Scenario: Cancelling a large copy mid-batch
- **WHEN** a user cancels a batch copy operation while items remain unprocessed
- **THEN** items already copied before the cancellation SHALL remain at the destination, remaining items SHALL NOT be copied, and FastFiles SHALL report the operation as cancelled with a count of completed items

#### Scenario: Cancellation does not corrupt in-flight state
- **WHEN** a user cancels an operation
- **THEN** FastFiles SHALL NOT forcibly terminate the worker thread or leave the destination in a partially-written, unreported state for the item that was in flight at the moment of cancellation

### Requirement: Non-Crashing Error Handling for Individual Item Failures
`FastFiles` SHALL handle failures on individual items within a batch operation — including locked/in-use files, permission-denied errors, and source items that no longer exist at the time they are processed — without crashing and without aborting the rest of the batch. Each such failure SHALL be recorded and surfaced in a summary once the batch completes.

#### Scenario: A file is locked by another process during a batch copy
- **WHEN** a batch copy operation reaches an item that is locked by another process and cannot be read
- **THEN** FastFiles SHALL record the failure for that item, continue processing the remaining items in the batch, and SHALL NOT crash or hang

#### Scenario: A source item vanishes before it is processed
- **WHEN** a batch operation reaches an item that was listed at the start of the operation but no longer exists on disk
- **THEN** FastFiles SHALL record the failure for that item, continue processing the remaining items, and SHALL NOT crash

#### Scenario: Batch completes with some failures
- **WHEN** a batch operation finishes with one or more recorded per-item failures
- **THEN** FastFiles SHALL show a single non-blocking summary indicating how many items succeeded and how many failed, with access to per-item failure detail

### Requirement: Reversible Operation History
`FastFiles` SHALL maintain an in-memory history of completed rename, move, and Recycle-Bin-delete operations, sufficient to reverse each one via an Undo command, for the duration of the running session.

#### Scenario: Undoing a move
- **WHEN** a user invokes Undo immediately after a completed move operation
- **THEN** the moved items SHALL be moved back to their original location

#### Scenario: Undoing a rename
- **WHEN** a user invokes Undo immediately after a completed rename operation
- **THEN** the item SHALL be renamed back to its original name

#### Scenario: Undoing a Recycle-Bin delete
- **WHEN** a user invokes Undo immediately after a completed Recycle-Bin delete operation
- **THEN** the deleted items SHALL be restored from the Recycle Bin to their original location

#### Scenario: Undo history does not survive process restart
- **WHEN** FastFiles is closed and reopened
- **THEN** the operation history from the prior session SHALL NOT be available for Undo

### Requirement: Irreversible Operations Are Never Recorded in Undo History
Permanent deletion and any operation that overwrites an existing destination item (a "Replace" conflict resolution) SHALL NEVER be added to the reversible operation history under any configuration or circumstance, because FastFiles cannot reconstruct the destroyed data.

#### Scenario: Permanent delete is not undoable
- **WHEN** a user performs a permanent delete and then invokes Undo
- **THEN** FastFiles SHALL NOT reverse the permanent delete, and the permanently deleted items SHALL NOT appear anywhere in the undo history

#### Scenario: An overwrite from a Replace conflict resolution is not undoable
- **WHEN** a copy or move operation overwrites an existing destination item because the user chose Replace during conflict resolution
- **THEN** that overwrite SHALL NOT be added to the undo history, and invoking Undo afterward SHALL NOT restore the overwritten item's prior content

### Requirement: Index Invalidation Notification on Completion
Upon completion of a copy, move, rename, delete, or create operation, `FastFiles` SHALL notify `FastFilesEngine` of the affected paths so cached index entries can be refreshed, on a best-effort basis that never blocks or delays reporting the file operation's own completion to the user.

#### Scenario: Engine notification does not block operation completion
- **WHEN** a file operation completes and `FastFilesEngine` is unreachable or not running
- **THEN** FastFiles SHALL still report the file operation as complete to the user without waiting for or being blocked by the notification attempt
