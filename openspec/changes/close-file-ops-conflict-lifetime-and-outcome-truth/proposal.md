## Why

The 2026-09-05 post-certification audit reproduced a shutdown use-after-free on the in-app conflict prompt (PA-010) and a failure-details surface that can never show failures (PA-013). Both are still present on `046dead`: `FileOperations::RequestConflictDecision` posts a raw pointer to a worker-stack `unique_ptr` that is destroyed when `Stop()` observes `stopping_` during `ConflictDialog`'s nested `GetMessageW` pump, and `lastOperationFailures_` is assigned only from Progress events that always carry an empty failures vector. `file-operations-core` already requires "access to per-item failure detail" after a mixed batch; the menu path does not implement it. Skip accounting (PA-024) and cancel-flag reset (PA-025) sit on the same worker/outcome contract.

## What Changes

- Give conflict-prompt state a lifetime that outlives `Stop()` / `WM_DESTROY` until the UI handler has published a decision (or an explicit cancelled decision). Destroying the FastFiles window while the conflict dialog is up SHALL NOT free the question object the handler still writes.
- Record terminal (Completed/Cancelled) per-item failures into the last-operation-errors surface. Progress events remain progress-only.
- Count skipped items in the batch outcome so "N of M" and the failure/skip summary match the user's selection, not only IFileOperation callbacks.
- Reset `cancelRequested_` only at the start of a newly dequeued operation, so an Esc that races an enqueue cannot pre-cancel the next batch.
- Add regression tests that the current suite does not have: Stop during an in-flight conflict prompt; terminal events populate failure detail; skip counts; cancel-vs-enqueue.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `file-operations-core`: conflict-prompt lifetime vs window teardown; last-operation failure detail sourced from terminal events; skip accounting; cancel-flag scope.
- `conflict-resolution`: the in-app conflict dialog remains the prompt, but a cancelled/tearing-down owner SHALL resolve the outstanding question without use-after-free.

## Impact

- `src/ui/src/FileOperations.{h,cpp}`, `ConflictDialog.cpp`, `WindowShell.cpp` (WM_APP_FILE_OPERATION_*), `FileOperationPolicy.cpp` (skip accounting).
- Tests: `tests/fileoperations/` (extend e2e / policy) — no nested-pump+Stop coverage today.
- No protocol, engine, or privilege-boundary change.
- Non-goals: OLE drag-out MOVE (separate change), clipboard GlobalLock (separate), workspace multi-writer (separate), PA-027 file-type `std::async` (intentional non-join for network `SHGetFileInfoW`; owner-token teardown remains deferred).

## Open Questions

- None. Lifetime is shared/heap ownership with UI-side completion; failure detail is assigned from terminal events; skip counts are first-class in the outcome struct.
