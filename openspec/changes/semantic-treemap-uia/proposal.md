# Treemap Semantic UIA, Accessibility, and Geometry Truthfulness

## Why

The Storage Analysis Treemap is the last major custom-rendered FastFiles
surface with no UI Automation presence. The live UIA capability records:

    treemap-readability-click-precision  SKIPPED  treemap-drawn-on-d2d-surface

because the Direct2D treemap has no semantic provider, no geometry, and no
activation path a UIA client can observe. This change exposes the treemap as
a real semantic UI Automation surface integrated into the existing
main-window fragment root (one root — no second fragment, no per-tile HWNDs),
and replaces the historical SKIP with real end-to-end assertions.

While implementing the semantic projection, the treemap and its drill-down
sibling were audited against the canonical `storage-drilldown-and-treemap`
specification. The audit exposed four product defects that directly affect
the geometry and metadata the provider is supposed to expose. They are fixed
as part of this change:

1. **Treemap hover metadata gap.** The canonical spec requires hover to show
   full path, size, percentage of parent, and percentage of volume. The
   current renderer shows only `Size:` and `Size on disk:` inside the tile.
   Historical storage-analysis task 4.5 claimed this complete; the claim is
   false (see evidence).
2. **Drill-down `% of Volume` gap.** The canonical spec requires every
   drill-down item to show percentage of parent AND percentage of volume.
   The storage ListView shows `% of Parent` only; historical task 3.2's claim
   that percentage-of-volume display is integrated is false (see evidence).
3. **Small-item layout defect.** Historical task 4.3's "other small items"
   catch-all was never implemented; `TreemapLayout` clamps every rectangle to
   `kMinRectSize`, which provably produces overlapping rectangles, out-of-
   container rectangles, and runaway covered area with many tiny siblings
   (deterministic fixture evidence in evidence/).
4. **File-tile activation defect.** The canonical spec requires clicking a
   file rectangle to open the standard column-view surface at the file's
   containing folder with the file selected. The current shell callback
   instead runs `OpenWithDefaultApplication` — it launches/executes the file.
   Historical task 4.6's wording is not satisfied (see evidence).

No change is made to the privileged service, its protocol, Authenticode
trust, or production signing. This is purely UI/automation work plus the
product-behavior corrections above.

## What changes

- A pure semantic projection of the visible treemap state
  (`TreemapSemantics.h` / a `TreemapUiaSurface` seam implemented by
  `StorageAnalysis`), lock-protected, reflecting the exact cached layout
  used by rendering and hit-testing — no parallel accessibility data model.
- A shared metadata formatter (`BuildTreemapItemMetadata`) used by BOTH the
  D2D hover presentation and UIA metadata properties, so text can never
  drift.
- A `TreemapUiaProvider` tree: Group "Storage Treemap" (AutomationId
  `Storage.Treemap`) hosting DataItem tiles, all inside the existing
  `ColumnViewUiaProvider` fragment root. No second fragment root.
- On-demand, bounded tile providers with stable semantic runtime identity
  (FRN when present, path fallback), `UiaOwnerState` lifetime, and
  `UIA_E_ELEMENTNOTAVAILABLE` stale semantics (aggregate-resolution
  exception documented).
- `IInvokeProvider` on real tiles only; Invoke marshals to the UI thread via
  `PostMessage` carrying value identity (never raw pointers), then runs the
  exact mouse-click navigation path. No Selection pattern (the treemap has
  no selection model). Honest keyboard-focus reporting.
- `ElementProviderFromPoint` parity: the fragment root hit-tests the same
  cached layout via the same pure math as `TreemapView::HandleLButtonDown`.
- Product fixes: catch-all aggregation + exact-tiling layout in
  `TreemapLayout.h`, full hover metadata, `% of Volume` column in the
  storage drill-down (sharing `StoragePercentage`), and the file-activation
  navigation fix.
- The live scenario `treemap-readability-click-precision` moves into an
  isolated scenario runner with real assertions (discovery, proportionality,
  click precision, folder/file Invoke, category filter, resize, many-small-
  files, filesystem non-mutation).

## User impact

- Screen-reader and UIA clients can discover, read metadata about, and
  activate treemap tiles without pixel assistance.
- Hover on a treemap tile now shows the full path and both percentages the
  canonical spec has always required.
- The storage drill-down list gains the missing `% of Volume` column.
- Treemaps with hundreds of tiny siblings no longer render overlapping or
  overflowing rectangles; tiny items aggregate into an honest
  "Other small items" tile.
- Clicking a file tile in the treemap (or a file row in the storage list)
  now opens the containing folder with the file selected instead of
  launching the file.

## Out of scope

- Privileged MFT/USN production validation, certificates, trust stores,
  Authenticode pins.
- Any metric toggle (allocated vs logical), destructive file operations,
  OCR/pixel automation, or a redesign of the Storage Analysis UI.
- Widening indexed search to allocated size.
