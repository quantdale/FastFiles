## 1. Conflict prompt lifetime

- [ ] 1.1 Replace worker-stack unique_ptr-plus-`.get()` posting in `FileOperations::RequestConflictDecision` with shared ownership that stays alive until the UI handler has published a decision (design D1). Keep `PostEvent`'s release-after-PostMessage pattern for events.
- [ ] 1.2 Keep `FileOperations::Stop` PeekMessage drain for still-queued `WM_APP_FILE_OPERATION_CONFLICT` messages; publish Cancel into the still-alive question; join the worker without a hang.
- [ ] 1.3 Ensure `WindowShell` WM_DESTROY / nested `ShowConflictDialog` pump cannot write the question after free; teardown resolves as Cancel (conflict-resolution spec).

## 2. Terminal failure detail and skip/cancel accounting

- [ ] 2.1 Assign `lastOperationFailures_` from Completed and Cancelled events only; do not let Progress events with empty `failures` overwrite a terminal list (design D2).
- [ ] 2.2 Add a skipped count on `FileOperationEvent` populated from `BuildTransferPlan` Skip decisions; surface it in the completion title/notice so an all-skip batch is not "0 of 0" (design D3).
- [ ] 2.3 Clear `cancelRequested_` when dequeuing a new request after the previous cancel was applied, not only when the queue is observed empty (design D4).

## 3. Tests

- [ ] 3.1 Add a file-operations test that posts a conflict question, calls Stop (or destroys the owner) before the dialog answers, and asserts the worker joins, the decision is Cancel, and ASAN/the process does not touch freed memory.
- [ ] 3.2 Assert a Completed event with a non-empty failures vector is what the last-operation-errors surface reads; a Progress event with `{}` does not wipe it.
- [ ] 3.3 Assert skip counts for all-skip and mixed-skip batches; assert a new enqueue after cancel is not born cancelled.
- [ ] 3.4 Run `cmake --build --preset debug --target fffileoperations_e2e_tests fffileoperations_tests` and `ctest --test-dir build/debug -R fffileoperations --output-on-failure`.
