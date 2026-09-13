## Context

See proposal.md. `BeginFileDrag` (`OleDragDrop.cpp`) runs `DoDragDrop` with COPY|MOVE|LINK and returns `effect`. `WindowShell` WM_MOUSEMOVE discards it. `FileDataObject` is CF_HDROP-only; no `CFSTR_PREFERREDDROPEFFECT` / `CFSTR_PERFORMEDDROPEFFECT`. Intra-app drops already `QueueTransfer` Move/Copy and are correct. Link-drop in `WindowShell` names shortcuts with `exists` only, no intra-batch reservation.

## Goals / Non-Goals

**Goals:**
- Complete drag-out MOVE iff the target did not already remove sources.
- Unique `.lnk` names for two same-basename link sources.

**Non-Goals:**
- Changing `DetermineDropEffect` defaults.
- Clipboard HDROP lock.
- Replacing CF_HDROP with `CFSTR_FILECONTENTS` (not required for filesystem paths).

## Decisions

### D1: Honor OLE performed-drop-effect, do not blindly delete on MOVE

After `DoDragDrop`:
1. Read `CFSTR_PERFORMEDDROPEFFECT` from the data object if present.
2. If the effective effect is MOVE and each source path still exists, enqueue Recycle-Bin delete of those sources through `FileOperations` (same worker as menu Delete).
3. If a source no longer exists, skip it (target already moved).

**Why:** Explorer typically moves HDROP paths itself. Blind delete would double-delete. Targets that copy-then-return MOVE need source delete. This is the documented OLE protocol.

**Rejected:** Always delete on `DROPEFFECT_MOVE` — wrong for Explorer. Never delete — current bug.

### D2: Advertise `CFSTR_PREFERREDDROPEFFECT` on the data object

Set preferred effect from `DetermineDropEffect` (already used by the drop target). Helps Explorer pick MOVE vs COPY.

### D3: Link-drop uses the same `plannedDestinations` reservation as `BuildTransferPlan`

Extract or reuse the destination-key set so two `report.txt` sources become `report.txt.lnk` and `report.txt (2).lnk` (or equivalent GenerateKeepBothName on the `.lnk`).

## Risks / Trade-offs

- **[Risk] Recycle-Bin delete of sources after an Explorer MOVE that is still in flight.** → Mitigation: existence check immediately after `DoDragDrop` returns; Explorer MOVE is complete before return.
- **[Risk] User-visible Recycle-Bin prompt on drag-out.** → Mitigation: use the existing default-delete path with Recycle-Bin, FOF flags already used for silent recycle where the product does today. Do not use permanent delete.

## Migration Plan

No persisted state. Revert the UI binary to roll back.

## Open Questions

None.
