# Fast Files — Agent Workstate

Last updated: 2026-08-09T15:35:10+08:00
Status: COMPLETE

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD: `bf073b2ebe75f73d08c5b7b10e8b5149b39f071c`
Working tree: clean after the focused product and acceptance-evidence commits; runtime artifacts cleaned
Infrastructure commit: `4ca9e40` (`Add persistent Codex workstate protocol`)
Product commit: `2d671d1` (`Preserve selection identity across snapshot refresh`)

## Current Engineering Slice

Name: Stable selection identity across snapshot / watcher refresh

Objective: Preserve selection, anchor, focus, and operation targets by logical filesystem identity when snapshot rows are rebuilt, sorted, inserted, removed, or renamed.

Safety / correctness invariant: A refresh must never transfer selection from logical entry A to unrelated entry B because B inherited A's old numeric row index.

Non-goals: Storage Analysis, global shortcuts, shortcut persistence, Treemap filtering, watcher shutdown ordering, stale-snapshot UX, search scalability, MFT/USN activation, signature pins, service authentication, and installer work.

## Current Frontier

Implementation, deterministic tests, runtime watcher/reorder acceptance, required validation, scope audit, and focused product commit are complete. A fresh runtime acceptance rerun passed with the existing read-only Debug probe.

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

- None.

### Not Started

- Next recommended slice: Storage Analysis visibility/overlap. Do not start it automatically.

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

Debug: PASS; full `cmake --build --preset debug`.
Analyze: PASS; full `cmake --preset analyze` and `cmake --build --preset analyze` with `/analyze`.
CTest: PASS, 37/37 serial with process-local `TEMP=C:\Windows\Temp` and `TMP=C:\Windows\Temp`.
Runtime: PASS. In a fresh disposable `C:\Windows\Temp\FastFiles-SelectionIdentity-*` fixture, real FastFiles/Engine state showed A/B/C with B selected at row 1; external creation of `000-new.txt` produced `000-new.txt|A.txt|B.txt|C.txt`, B at row 2, B identity/path/focus retained, and `ActiveSelectionPaths()` operation target still B. All fixture folders and owned processes were cleaned.
Other: `ffdoc_drift_tests` initially caught mixed line endings in the edited source; changed files were normalized, final `git diff --check` passed, and the final full CTest passed.
Product commit: `2d671d107b5f38b5709e32cc2e7edaf5bbe42f28`.

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.
- No active blockers remain for this slice.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.

## NEXT EXACT ACTION

Slice is COMPLETE. Do not implement the next slice in this session. The single next recommended engineering slice is Storage Analysis visibility/overlap.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
