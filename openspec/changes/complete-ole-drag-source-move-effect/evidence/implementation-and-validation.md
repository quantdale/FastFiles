# Implementation and validation — complete-ole-drag-source-move-effect

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- `FileDataObject` advertises `CFSTR_PREFERREDDROPEFFECT` (from the modifier
  state at drag start) and accepts the target's `CFSTR_PERFORMEDDROPEFFECT`
  through `SetData`; `PerformedDropEffectFromDataObject` reads it back after
  `DoDragDrop` (design D1/D2).
- `BeginFileDrag(paths, keyState)` consumes the negotiated effect and prefers
  the target's published performed effect. `WindowShell`'s drag start now uses
  the result: a `MOVE` whose sources still exist enqueues them for the existing
  Recycle-Bin delete path, while sources the target already moved are left
  alone; `COPY`/`LINK` never delete and permanent delete is never used
  (design D1). A drop inside the FastFiles window sets
  `dragDropHandledInWindow_`, so the intra-app MOVE already queued through
  `QueueTransfer` is never re-completed as an external source delete.
- Link drops now plan destinations through the new `BuildLinkPlan`, which
  reserves planned names within the batch exactly like `BuildTransferPlan`, so
  two same-basename sources get distinct `.lnk` names (`report.txt.lnk` and
  `report.txt (2).lnk`) instead of one overwriting the other (design D3).

## Files

- `src/ui/src/OleDragDrop.{h,cpp}`
- `src/ui/src/WindowShell.{h,cpp}`
- `src/ui/src/FileOperationPolicy.{h,cpp}`
- `tests/fileoperations/test_file_operation_policy.cpp`

## Tests added

- Source-delete completion: MOVE with sources present returns them; MOVE with
  sources already gone returns none; COPY and LINK never return any; a partial
  existence check deletes only the still-present sources.
- Data object: the preferred drop effect is advertised and readable; a
  simulated target `SetData(PERFORMEDDROPEFFECT)` round-trips through
  `PerformedDropEffectFromDataObject`.
- Link planning: two same-basename sources produce `report.txt.lnk` +
  `report.txt (2).lnk`; with an occupied destination on disk the batch reserves
  `report.txt (2).lnk` + `report.txt (3).lnk`.

## Validation

- `ctest --test-dir build/debug -R fffileoperations --output-on-failure`:
  `fffileoperations_tests` PASS (includes the new drag/link assertions).
- `ctest --test-dir build/debug -R ffdrop_destination --output-on-failure`:
  PASS.

## Honest boundaries

- No live Explorer drag-out e2e: the effect-completion decision and data-object
  protocol are covered headlessly; the real `DoDragDrop` interaction itself is
  the same call path as before.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
