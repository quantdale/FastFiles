## Context

See proposal.md for motivation. Current seams:

- `FileOperations::RequestConflictDecision` (`src/ui/src/FileOperations.cpp`) allocates `unique_ptr<FileOperationConflictQuestion>`, posts `question.get()` via `WM_APP_FILE_OPERATION_CONFLICT`, waits on `answered || stopping_`. Contrast: `PostEvent` correctly `release()`s after a successful `PostMessageW`.
- UI handler (`WindowShell.cpp` `WM_APP_FILE_OPERATION_CONFLICT`) runs `ShowConflictDialog`, which nested-pumps `GetMessageW(nullptr,…)`. `WM_DESTROY` calls `fileOperations_.Stop()` then `PostQuitMessage`. `Stop()` already drains *queued* conflict messages; it cannot drain a message already inside the nested pump.
- `lastOperationFailures_` is assigned only in the Progress branch; `ProgressSink::UpdateProgress`/`PreItem` pass `{}` failures. Terminal events carry the real vector.
- `BuildTransferPlan` Skip is `continue` with no accounting. `cancelRequested_` is cleared only when the queue is observed empty.

## Goals / Non-Goals

**Goals:**
- Conflict question lifetime that is safe across Stop/WM_DESTROY.
- Last-operation-errors populated from terminal events.
- Skip counts in the outcome; cancel flag scoped to the batch that observed it.

**Non-Goals:**
- Replacing `IFileOperation` or the in-app dialog.
- OLE drag-out MOVE, clipboard lock, workspace merge, file-type `std::async` (PA-027: intentional non-join for network `SHGetFileInfoW`).

## Decisions

### D1: Shared ownership of `FileOperationConflictQuestion` until both sides are done

The worker and the UI handler SHALL share ownership (`std::shared_ptr` posted as a heap-stable pointer the UI also owns, or `unique_ptr` released to the UI with the worker waiting on a copy of the CV inside the shared object). The object is destroyed only after `answered == true` **and** the UI handler has returned from the dialog path (or Stop has published Cancel into a still-alive object).

**Why:** Posting `.get()` of a worker-stack unique_ptr is the UAF. `PostEvent` already shows the correct pattern for events (`release` after PostMessage). Stop's PeekMessage drain stays as a fast path for *queued* prompts.

**Rejected:** Making the conflict dialog modeless — larger UX change, not required to close the UAF. Joining the worker before destroying the HWND without answering the prompt — hangs on the 100 ms wait loop until stopping_ is set *and* still UAF if unique_ptr is the lifetime.

**Trade-off:** One extra heap allocation per conflict; acceptable.

### D2: Assign `lastOperationFailures_` on Completed and Cancelled only

Progress events keep an empty failures vector (the sink does not snapshot `failures_` on every tick). WindowShell copies `event->failures` on terminal events and clears on a later successful terminal with empty failures.

**Why:** Matches the existing event shape; smallest correct fix. Putting failures on Progress would require locking the sink's vector on every tick.

### D3: Outcome struct carries `skipped` separately from `completed`/`total`

`FileOperationEvent` gains a skipped count (and optionally skipped paths). `BuildTransferPlan` records skips instead of dropping them. The title/notice uses completed + skipped + failures vs original source count.

**Why:** Skip is a user decision, not a failure. Inflating `total` via `PostItem` directory recursion stays a known IFileOperation quirk; this change does not rewrite progress percentages.

### D4: Clear `cancelRequested_` when dequeuing a new request, not when the queue is empty

The empty-queue reset is what lets Esc latch into the next enqueue. Clearing at dequeue start (after observing the flag for *that* request) scopes cancel to work that was queued or running when Esc happened.

**Why:** Matches the spec scenario "a new batch after Esc is not born cancelled."

## Risks / Trade-offs

- **[Risk] Nested pump still dispatches WM_DESTROY.** → Mitigation: D1 keeps the question alive; Stop publishes Cancel before join; dialog loop should exit on `!IsWindow(dialog)` after owner destroy. Verify with a unit that posts conflict then Stop from the same UI thread.
- **[Risk] shared_ptr posted as LPARAM if the UI never runs the handler.** → Mitigation: Stop's PeekMessage drain plus destructor of the worker's shared_ptr; if PostMessage fails, worker already returns `{}` today.
- **[Risk] Skip count vs IFileOperation PostItem inflation.** → Mitigation: skipped is counted from the plan, not from the sink; document that percent still comes from the sink.

## Migration Plan

In-process behavior change; no on-disk format. Ship with the UI binary. Rollback is revert.

## Open Questions

None.
