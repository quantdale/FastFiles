## 1. Drag-out MOVE completion

- [x] 1.1 Advertise `CFSTR_PREFERREDDROPEFFECT` on `FileDataObject` from `DetermineDropEffect` (design D2).
- [x] 1.2 After `DoDragDrop`, read `CFSTR_PERFORMEDDROPEFFECT` and the returned effect; if MOVE and sources still exist, Recycle-Bin-delete them via `FileOperations`; if sources are already gone, skip (design D1). Consume the return in `WindowShell` instead of discarding `BeginFileDrag`.
- [x] 1.3 Do not delete sources on COPY or LINK; do not use permanent delete for this path.

## 2. Link-drop name reservation

- [x] 2.1 Apply intra-batch `plannedDestinations` (or `GenerateKeepBothName`) to the WindowShell link-drop planner so two same-basename sources do not overwrite one `.lnk` (design D3).

## 3. Tests

- [x] 3.1 Headless test: simulated MOVE with sources still present enqueues source delete; simulated MOVE with sources already absent does not.
- [x] 3.2 Headless test: COPY effect never deletes sources.
- [x] 3.3 Two same-basename link sources produce two distinct `.lnk` names.
- [x] 3.4 Run focused UI/file-operations tests: `ctest --test-dir build/debug -R "ffdrop|fffileoperations" --output-on-failure`.
