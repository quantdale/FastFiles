# Fast Files — Agent Workstate

Last updated: 2026-08-09T14:08:00+08:00
Status: ACTIVE

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD: `4ca9e4097432bfa252161ad62f20c12400d2f5e7`
Working tree: product slice and narrow debug runtime-observability changes present; memory infrastructure committed separately; runtime artifacts cleaned
Infrastructure commit: `4ca9e40` (`Add persistent Codex workstate protocol`)

## Current Engineering Slice

Name: Stable selection identity across snapshot / watcher refresh

Objective: Preserve selection, anchor, focus, and operation targets by logical filesystem identity when snapshot rows are rebuilt, sorted, inserted, removed, or renamed.

Safety / correctness invariant: A refresh must never transfer selection from logical entry A to unrelated entry B because B inherited A's old numeric row index.

Non-goals: Storage Analysis, global shortcuts, shortcut persistence, Treemap filtering, watcher shutdown ordering, stale-snapshot UX, search scalability, MFT/USN activation, signature pins, service authentication, and installer work.

## Current Frontier

Implementation, deterministic tests, and runtime watcher/reorder acceptance are complete. Final Debug/Analyze/full-CTest rerun, scope audit, product commit, and final WORKSTATE bookkeeping remain.

## Established Facts

- `Column` owns independent `selectedIndices`, `selectionAnchor`, and `focusIndex`; `columns_` and `columns2_` provide separate pane state.
- `RefreshColumnFromSnapshot` rebuilt/sorted `column.items`, restored selection by case-insensitive name, and could leave `focusIndex` at an unrelated row when the focused item disappeared.
- `ActiveSelectionPaths`, `CurrentSelectionSummary`, command dispatch, drag start, open, rename, delete, copy, cut, move/paste, and context actions resolve paths from the active column's current selected indices.
- Degraded enumeration entries carry no stable file ID; indexed snapshot entries carry `volumeRowId`, `fileIdLow`, and `fileIdHigh` from the existing index projection.
- Indexed identity is valid only when both a nonzero volume row and nonzero FRN data are present. Degraded identity is normalized full path, compared case-insensitively with Windows ordinal comparison.
- If both sides have stable indexed identity, matching requires volume row plus FRN. If either side has stable indexed identity and the other does not, no path downgrade is allowed; path matching is used only when neither side has stable identity.
- Error/missing directory refreshes now clear items, selection, anchor, and focus.
- Search-result pending selection still uses the existing path/name navigation mechanism; the refreshed column state now uses the same identity-safe remapping once entries are materialized.
- Debug-only `WM_COPYDATA` observation reads the active path, visible row names, selected/focused identity and row, and the existing `ActiveSelectionPaths()` operation target; it has no selection-control path.

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
- `src/ui/include/ffui/SelectionDiagnostics.h`, `ColumnView::CurrentSelectionDiagnostics`, and `tests/ui/selection_identity_runtime_probe.cpp` provide the minimal read-only runtime observation seam.

### In Progress

- Final required build/test rerun, source scope audit, product commit, and WORKSTATE completion.

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
- `src/ui/include/ffui/SelectionDiagnostics.h`, `src/ui/src/ColumnView.h`, `src/ui/src/WindowShell.cpp`
  - Debug-only read-only WM_COPYDATA state query for runtime acceptance.
- `tests/ui/selection_identity_runtime_probe.cpp`
  - Small native query client; not registered as a CTest test.

## Tests Added / Modified

- `ffselection_identity_tests`
  purpose: Covers all eight required selection-identity scenarios.
  status: PASS.

## Validation

Debug: PASS; final changed UI and test targets built successfully.
Analyze: PASS; final changed UI and test targets built successfully with `/analyze`.
CTest: PASS, 37/37 serial with process-local `TEMP=C:\Windows\Temp` and `TMP=C:\Windows\Temp`.
Runtime: PASS. In a disposable `C:\Windows\Temp\FastFiles-SelectionIdentity-*` fixture, real FastFiles/Engine state showed A/B/C with B selected at row 1; external creation of `000-new.txt` produced `000-new.txt|A.txt|B.txt|C.txt`, B at row 2, B identity/path/focus retained, and `ActiveSelectionPaths()` operation target still B. All fixture folders and owned processes were cleaned.
Other: `ffdoc_drift_tests` initially caught mixed line endings in the edited source; changed files were normalized, final `git diff --check` passed, and the final full CTest passed.

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.

## NEXT EXACT ACTION

Run Debug, Analyze, and full serial CTest through the VS developer shell, then audit/stage only this selection slice and create the focused product commit; update this file to COMPLETE and stop.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
