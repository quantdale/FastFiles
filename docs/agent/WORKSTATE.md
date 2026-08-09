# Fast Files — Agent Workstate

Last updated: 2026-08-09T13:23:54+08:00
Status: ACTIVE

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD: `05b7bb14b0e45dc2f5b2e5b42b3d941137eb7635`
Working tree: product slice changes present; memory infrastructure not yet committed

## Current Engineering Slice

Name: Stable selection identity across snapshot / watcher refresh

Objective: Preserve selection, anchor, focus, and operation targets by logical filesystem identity when snapshot rows are rebuilt, sorted, inserted, removed, or renamed.

Safety / correctness invariant: A refresh must never transfer selection from logical entry A to unrelated entry B because B inherited A's old numeric row index.

Non-goals: Storage Analysis, global shortcuts, shortcut persistence, Treemap filtering, watcher shutdown ordering, stale-snapshot UX, search scalability, MFT/USN activation, signature pins, service authentication, and installer work.

## Current Frontier

Implementation and deterministic tests are complete. Final runtime watcher/reorder acceptance is not yet verified; the interrupted UI experiment used `C:\Windows\Temp\FastFiles-SelectionIdentity-Slice` and did not produce reliable selection evidence.

## Established Facts

- `Column` owns independent `selectedIndices`, `selectionAnchor`, and `focusIndex`; `columns_` and `columns2_` provide separate pane state.
- `RefreshColumnFromSnapshot` rebuilt/sorted `column.items`, restored selection by case-insensitive name, and could leave `focusIndex` at an unrelated row when the focused item disappeared.
- `ActiveSelectionPaths`, `CurrentSelectionSummary`, command dispatch, drag start, open, rename, delete, copy, cut, move/paste, and context actions resolve paths from the active column's current selected indices.
- Degraded enumeration entries carry no stable file ID; indexed snapshot entries carry `volumeRowId`, `fileIdLow`, and `fileIdHigh` from the existing index projection.
- Indexed identity is valid only when both a nonzero volume row and nonzero FRN data are present. Degraded identity is normalized full path, compared case-insensitively with Windows ordinal comparison.
- If both sides have stable indexed identity, matching requires volume row plus FRN. If either side has stable indexed identity and the other does not, no path downgrade is allowed; path matching is used only when neither side has stable identity.
- Error/missing directory refreshes now clear items, selection, anchor, and focus.
- Search-result pending selection still uses the existing path/name navigation mechanism; the refreshed column state now uses the same identity-safe remapping once entries are materialized.

## Decisions

- decision: Add a small header-only `ffui` identity/capture/restore seam instead of a global selection subsystem.
  rationale: Reuse existing index identity fields and current path data while keeping the behavior headless and deterministic.
  consequence: `ColumnView` retains index-based rendering/storage, but indices are regenerated only after identity matching.
- decision: Preserve indexed rename continuity only when volume+FRN proves continuity.
  rationale: A same-path replacement must not inherit an old indexed selection.
  consequence: Degraded mode follows full path; indexed-to-degraded downgrade clears safely.
- decision: Keep infrastructure and product commits separate.
  rationale: The workstate protocol must survive independently from the unfinished product slice.
  consequence: Commit only `AGENTS.md` and `docs/agent/WORKSTATE.md` first.

## Implementation State

### Completed

- Root cause trace across ColumnView, snapshot projection, degraded enumeration, search selection, and operation-target resolution.
- `src/ui/include/ffui/SelectionIdentity.h` identity and remapping seam.
- `ColumnView::RefreshColumnFromSnapshot` identity capture/restore for selection, anchor, and focus.
- Safe clearing on AccessDenied, NotFound, I/O error, and directory error callbacks.
- Eight focused deterministic regression scenarios in `tests/ui/test_selection_identity.cpp`.
- CTest registration in `tests/ui/CMakeLists.txt`.

### In Progress

- Disposable runtime watcher/reorder acceptance and final product-slice review.
- Memory infrastructure installation and isolated infrastructure commit.

### Not Started

- Product commit for the selection slice.

## Files Changed In This Slice

- `src/ui/src/ColumnView.cpp`
  - Identity-safe refresh, selection/anchor/focus restoration, and safe error clearing.
- `src/ui/include/ffui/SelectionIdentity.h`
  - Pure path/index identity and selection-state capture/restore helpers.
- `tests/ui/test_selection_identity.cpp`
  - Deterministic insertion, reorder, deletion, focus, degraded, indexed, and operation-target tests.
- `tests/ui/CMakeLists.txt`
  - Registers `ffselection_identity_tests`.

## Tests Added / Modified

- `ffselection_identity_tests`
  purpose: Covers all eight required selection-identity scenarios.
  status: PASS.

## Validation

Debug: PASS; changed UI and test targets built successfully.
Analyze: PASS; changed UI and test targets built successfully with `/analyze`.
CTest: PASS, 37/37 serial with process-local `TEMP=C:\Windows\Temp` and `TMP=C:\Windows\Temp`.
Runtime: FastFiles and FastFilesEngine launched; disposable-folder selection/reorder evidence is NOT VERIFIED. Screenshots were inconclusive for selection, and no user files were touched.
Other: `ffdoc_drift_tests` initially caught mixed line endings in the edited source; changed files were normalized and the final full CTest passed.

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- Runtime UI automation through the current headless/Direct2D environment has not yet yielded reliable row-selection evidence; do not claim runtime selection acceptance without a new proof.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.

## NEXT EXACT ACTION

Complete or explicitly bound the runtime acceptance: use the disposable `C:\Windows\Temp\FastFiles-SelectionIdentity-Slice` folder to select `B.txt`, create `000-new.txt` externally, and verify selection/Copy target; if the environment cannot expose reliable selection evidence after one changed method, record runtime as NOT VERIFIED, clean the owned process/temp artifacts, then perform final diff review and product validation before deciding on the product commit.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
