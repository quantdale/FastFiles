## ADDED Requirements

### Requirement: Conflict Prompt Lifetime Survives Owner Teardown
While an in-app destination-conflict prompt is outstanding, the prompt's decision object SHALL remain valid until the UI thread has published a decision (including an implicit Cancel published because the owner window is being destroyed). `FastFiles` SHALL NOT destroy that object from the worker thread merely because cancellation or window teardown was requested. Closing the FastFiles window, posting quit, or calling operation Stop during the nested conflict-dialog message pump SHALL NOT crash, hang the worker join indefinitely, or write to freed memory.

#### Scenario: Window closed while the conflict dialog is visible
- **WHEN** a copy or move has posted a conflict prompt and the user (or the system) destroys the FastFiles owner window before Replace, Skip, Keep Both, or Cancel is chosen
- **THEN** the outstanding prompt SHALL be resolved as Cancel (no remaining transfers from that batch), the worker SHALL finish without waiting forever, and the process SHALL NOT access the prompt object after it is destroyed

#### Scenario: Stop during a still-queued conflict message
- **WHEN** a conflict prompt has been posted but the UI thread has not yet dispatched it, and Stop runs
- **THEN** FastFiles SHALL answer that prompt as Cancel so the worker is not blocked, and SHALL NOT leave a dangling pointer for a later dispatch

### Requirement: Last-Operation Failure Detail Comes From Terminal Events
After a batch Completes or is Cancelled, `FastFiles` SHALL retain the per-item failure list carried on that terminal event and SHALL present it through the existing last-operation-errors UI (the "Last operation errors" / operation-details command). Progress updates SHALL NOT replace that list with an empty vector. A subsequent batch that completes with zero failures SHALL clear the retained list.

#### Scenario: Failed batch then opening last-operation errors
- **WHEN** a batch finishes with one or more per-item failures and the user invokes the last-operation-errors command
- **THEN** FastFiles SHALL list those failed paths and error codes, and SHALL NOT report that there are no recent file-operation errors

#### Scenario: Progress ticks do not erase recorded failures
- **WHEN** Progress events are delivered during a batch that later completes with failures
- **THEN** those Progress events SHALL NOT cause the last-operation-errors command to show an empty list after the terminal event

### Requirement: Skipped Items Are Visible In Batch Outcome
Items the user skipped at conflict resolution SHALL be counted in the batch outcome separately from completed transfers and per-item failures. The completion summary SHALL NOT report a skipped-only batch as "0 of 0".

#### Scenario: All items skipped
- **WHEN** a user copies three items, every name collides, and the user chooses Skip for all
- **THEN** FastFiles SHALL report that three items were skipped and zero were transferred, rather than "0 of 0"

#### Scenario: Mixed skip and success
- **WHEN** a five-item copy skips two names and transfers the other three
- **THEN** the summary SHALL reflect three completed and two skipped (and any failures besides)

## MODIFIED Requirements

### Requirement: Operation Cancellation
`FastFiles` SHALL allow a user to cancel an in-progress copy, move, or delete batch operation. Cancellation SHALL be cooperative: items already completed at the time of cancellation SHALL remain completed and unaffected, and the operation SHALL report a clear "cancelled" state reflecting how many items completed versus were not attempted. A cancel request SHALL apply to the currently executing batch and to work already queued when cancel was observed; it SHALL NOT latch across a later, distinct batch that the user enqueues after cancel was consumed.

#### Scenario: Cancelling a large copy mid-batch
- **WHEN** a user cancels a batch copy operation while items remain unprocessed
- **THEN** items already copied before the cancellation SHALL remain at the destination, remaining items SHALL NOT be copied, and FastFiles SHALL report the operation as cancelled with a count of completed items

#### Scenario: Cancellation does not corrupt in-flight state
- **WHEN** a user cancels an operation
- **THEN** FastFiles SHALL NOT forcibly terminate the worker thread or leave the destination in a partially-written, unreported state for the item that was in flight at the moment of cancellation

#### Scenario: A new batch after Esc is not born cancelled
- **WHEN** a user cancels an in-flight or queued batch and then, after that cancel has been applied, enqueues a new copy or move
- **THEN** the new batch SHALL run to completion unless the user cancels it separately

### Requirement: Non-Crashing Error Handling for Individual Item Failures
`FastFiles` SHALL handle failures on individual items within a batch operation — including locked/in-use files, permission-denied errors, and source items that no longer exist at the time they are processed — without crashing and without aborting the rest of the batch. Each such failure SHALL be recorded on the batch's terminal Completed or Cancelled event and surfaced in a summary once the batch completes, including access to per-item failure detail from that terminal list.

#### Scenario: A file is locked by another process during a batch copy
- **WHEN** a batch copy operation reaches an item that is locked by another process and cannot be read
- **THEN** FastFiles SHALL record the failure for that item, continue processing the remaining items in the batch, and SHALL NOT crash or hang

#### Scenario: A source item vanishes before it is processed
- **WHEN** a batch operation reaches an item that was listed at the start of the operation but no longer exists on disk
- **THEN** FastFiles SHALL record the failure for that item, continue processing the remaining items, and SHALL NOT crash

#### Scenario: Batch completes with some failures
- **WHEN** a batch operation finishes with one or more recorded per-item failures
- **THEN** FastFiles SHALL show a single non-blocking summary indicating how many items succeeded and how many failed, with access to per-item failure detail taken from the terminal event
