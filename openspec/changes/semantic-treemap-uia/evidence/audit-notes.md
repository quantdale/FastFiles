# Evidence — semantic-treemap-uia audit

This directory records the pre-change audit evidence. Historical task
checkboxes are NOT rewritten; each entry below records what the historical
evidence claimed, what the later semantic validation exposed, the exact
present defect with code references, the corrective change, and its
validation.

## Baseline (pre-change)

Full capability run `verify/runs/ad-hoc/20260816-154811` on
`07ece08989fde662162dc8b8eebdf84266673bc2`:

- **20 PASS / 6 SKIP / 0 FAIL**, overall PASS.
- `treemap-readability-click-precision` → `SKIPPED`,
  `reason=treemap-drawn-on-d2d-surface` ("Treemap remains a Direct2D surface
  without a UIA provider in this change").
- Other SKIPs (independent, preserved): `in-column-error-state`
  (engine-connected-host), `storage-mid-scan-calculating`
  (privileged-scan-unavailable), `search-toggle-service-while-open`
  (privileged-scan-unavailable), `drag-in-from-explorer`
  (explorer-drag-source-unavailable), `in-column-error-state-late`
  (error-state-trigger-not-deterministic).
- Filtered run (`UiaScenario=treemap-readability-click-precision`,
  run `20260816-155329`): the harness executes the whole shared session for
  non-isolated filtered scenarios (harness quirk); that run hit transient
  engine-connect failures (engine-connect-unavailable SKIPs and cascading
  shared-session FAILs). The treemap scenario itself still recorded
  `SKIPPED / treemap-drawn-on-d2d-surface` in both runs. The treemap
  scenario is therefore moved to an ISOLATED runner in this change.

## D1 — Hover metadata gap (historical task 4.5)

- **Historical claim:** storage-analysis task 4.5 marked "hover metadata"
  complete.
- **Code:** `TreemapView::Render` (src/ui/src/TreemapView.cpp:350-375)
  draws, when hovered, only `Size: <logical>` and `Size on disk: <…>` —
  no full path, no percentage of parent, no percentage of volume.
- **Canonical requirement:** spec "Treemap Hover Metadata": full path,
  size, % of parent, % of volume.
- **Present defect:** the spec-required hover content is absent.
- **Correction:** shared `BuildTreemapItemMetadata` (TreemapSemantics.h)
  drives both the D2D hover lines and UIA FullDescription; hover now shows
  path, size, allocated size with honest qualifiers, % parent, % volume.
- **Validation:** `fftreemap_metadata_tests`; live FullDescription
  assertions in `treemap-readability-click-precision`.

## D2 — Drill-down `% of Volume` gap (historical task 3.2)

- **Historical claim:** storage-analysis task 3.2 claimed
  "percentage-of-volume display integrated".
- **Code:** 7 columns (src/ui/src/StorageAnalysis.cpp:112):
  Name/Type/Size/Subtree Size/% of Parent/Modified/Size on Disk — no
  `% of Volume` anywhere.
- **Canonical requirement:** spec "Hierarchical Drill-Down with Percentage
  Context": every item shows % of parent AND % of overall volume.
- **Present defect:** `% of Volume` is never presented.
- **Correction:** 8th column `% of Volume` fed by the same
  `ComputeStoragePercentage` helper as `% of Parent`; volume denominator =
  OS reported used bytes of the containing volume (same ground truth as the
  Overview; spec: % of volume reflects "the total size of the whole
  volume").
- **Validation:** `ffstorage_percentage_tests` additions; live scenario
  asserts both percentage columns derive from UIA-readable cells where
  deterministic.

## D3 — Small-item/catch-all defect (historical task 4.3)

- **Historical claim:** task 4.3 stated items below the minimum-visible-size
  threshold use an "other small items" catch-all.
- **Code:** `TreemapLayout.h` clamps every generated rectangle to
  `max(kMinRectSize, ...)`; no catch-all exists.
- **Deterministic probe** (standalone compile of TreemapLayout.h, 96×96
  normalized container, kMinRectSize=4):

  | fixture | overlap pairs | out-of-container | covered area |
  |---|---|---|---|
  | 1 large + 10 medium + 100 tiny | 4950 | 100 | 49950% |
  | 300 equal tiny | 3328 | 0 | 120000% |
  | 1000 equal tiny | 38556 | 0 | 400000% |
  | 1 + 500 | 121590 | 500 | 80000% |
  | 8:4:2 | 0 | 0 | 100% |

- **Root cause:** `Squarify`'s band grouping converges to a single band per
  container (aspect ratio monotonically improves toward 1), so every rect
  height = share × container height; clamping tiny heights to 4.0 stacks
  them beyond the container (out-of-bounds) while `pos`/`innerPos` advance
  by the unclamped values, so clamped rects overlap their successors. The
  existing `fftreemap_layout_tests` only exercised 4-node and 2-node
  fixtures, so the defect was invisible to the suite.
- **Correction:** deterministic catch-all aggregation (≥2 sub-threshold
  siblings → synthetic `Other small items` node: combined logical size,
  member count, allocated known/total pair, non-navigable) and removal of
  all per-rect clamps so band math tiles exactly (zero overlap, full
  containment by construction); single-node case still fills its container;
  a lone sub-threshold node keeps honest sub-minimum geometry.
- **Validation:** expanded `fftreemap_layout_tests` (hundreds of tiny
  files, no-overlap/no-out-of-bounds invariants, catch-all membership,
  determinism); live many-tiny scenario via UIA geometry.

## D4 — File-tile activation defect (historical task 4.6)

- **Historical claim:** task 4.6 described file activation as opening the
  file browser at the containing folder with the file selected.
- **Code:** `WindowShell` navigate callback (src/ui/src/WindowShell.cpp:
  933-949) runs `OpenWithDefaultApplication(hwnd_, path)` for files — the
  file is launched/executed. `StorageAnalysis`'s treemap callback
  (StorageAnalysis.cpp:140-146) also sets `currentPath_` to the file path.
- **Canonical requirement:** spec "Treemap Click-to-Navigate": a file
  rectangle opens the standard column-view surface at the containing folder
  with the file selected, no file operation.
- **Present defect:** clicking a file tile launches the file instead of
  navigating.
- **Correction:** file activation reuses the established
  `item.open-containing-folder` path: hide storage, `NavigateWorkspace(
  parent_path, filename)` (same pattern as WindowShell.cpp:401-403); the
  treemap callback no longer sets `currentPath_` for files. The shared
  callback also governs drill-down list file activation, keeping one
  activation contract. Context-menu `Open` (`item.open`) remains the
  explicit launch path.
- **Validation:** headless routing tests where feasible + live scenario
  (file Invoke → containing folder, exact file selected, manifest
  unchanged).

## Corrective-change summary

| Defect | Fix location | Regression coverage |
|---|---|---|
| D1 hover metadata | `TreemapSemantics.h` + `TreemapView.cpp` | metadata tests, live FullDescription |
| D2 % of Volume | `StorageAnalysis.cpp` + `StoragePercentage` | percentage tests, live |
| D3 catch-all | `TreemapLayout.h` + `TreemapView.cpp` | layout tests, live many-tiny |
| D4 file activation | `WindowShell.cpp` + `StorageAnalysis.cpp` | live file-Invoke test |
