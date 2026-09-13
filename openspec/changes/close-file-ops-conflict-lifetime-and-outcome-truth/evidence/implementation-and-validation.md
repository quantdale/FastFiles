# Implementation and validation — close-file-ops-conflict-lifetime-and-outcome-truth

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- Conflict-question ownership: `FileOperations::RequestConflictDecision` now
  allocates `std::shared_ptr<FileOperationConflictQuestion>` and posts a
  heap-stable `FileOperationConflictQuestionRef` as the
  `WM_APP_FILE_OPERATION_CONFLICT` lParam. The UI handler and `Stop()`'s
  queued-message drain take ownership of that wrapper and publish the decision
  before releasing it; the worker keeps its own reference alive while it waits,
  so a `Stop()`/`WM_DESTROY` that runs inside the conflict dialog's nested
  message pump can no longer free the question under the UI thread's write
  (design D1).
- Last-operation errors: `RetainLastOperationFailures` retains the per-item
  failure vector from terminal (Completed/Cancelled) events only; a Progress
  tick with `{}` no longer wipes the list. `WindowShell` calls it and keeps the
  existing "Last operation errors" surface (design D2).
- Skip accounting: `FileOperationEvent` carries `skipped`; `Execute` counts the
  sources that received no plan item from `BuildTransferPlan` (all-skip reports
  `N` skipped instead of `0 of 0`; mixed reports completed + skipped over the
  original selection), and the shell title/notice surface it (design D3).
- Cancel scope: `FileOperations::CancelCurrent` no-ops while nothing is running
  or queued, marks every request already queued with `cancelledBeforeStart`, and
  the worker clears `cancelRequested_` after each batch (keeping it set during
  shutdown), so a new batch enqueued after a consumed cancel is not born
  cancelled (design D4).

## Files

- `src/ui/src/FileOperations.{h,cpp}`
- `src/ui/src/WindowShell.{h,cpp}`
- `tests/fileoperations/test_file_operations_e2e.cpp`

## Tests added

- Stop during an in-flight conflict prompt: the worker joins, the prompt
  resolves as Cancel, the destination is untouched, and answering the held
  question after `Stop()` does not touch freed memory (ASAN-visible class).
- Terminal-failure retention: a Completed event's failures survive a following
  Progress event, and a later clean terminal clears the retained list.
- All-skip and mixed-skip counts (`completed`/`skipped`/`total`).
- An idle Escape does not pre-cancel the next enqueued batch.

## Validation

- `ctest --test-dir build/debug -R fffileoperations --output-on-failure`:
  `fffileoperations_tests` PASS, `fffileoperations_e2e_tests` PASS (includes
  the new conflict-lifetime, retention, and skip/cancel assertions).
- `ffdoc_drift_tests` PASS after line-ending normalization.

## Honest boundaries

- The regression test simulates the dialog hold at the event-window seam rather
  than driving the real themed dialog; the existing policy test still covers the
  real dialog's decision plumbing.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
