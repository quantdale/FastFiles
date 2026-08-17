# Tasks — semantic-treemap-uia

Code comments cite tasks as `// semantic-treemap-uia N.x:` where the change
slug disambiguates across changes.

## 1. Baseline and audit

- [ ] 1.1 Run the full `windows-ui-automation-validation` capability on the
      unmodified tree and record the baseline: confirm
      `treemap-readability-click-precision` = SKIPPED
      (`treemap-drawn-on-d2d-surface`) and the overall PASS/SKIP/FAIL
      counts. (Expected baseline: 20 PASS / 6 SKIP / 0 FAIL — not
      hardcoded as a gate.)
- [ ] 1.2 Run the treemap scenario through the `UiaScenario=` filter and
      record the result (including the harness quirk that non-isolated
      filtered runs still execute the whole shared session).
- [ ] 1.3 Audit hover metadata against the canonical spec: record exactly
      what `TreemapView::Render` draws on hover today (label, `Size:`,
      `Size on disk:` — no path, no percentages) and the historical task
      4.5 claim. Evidence note in `evidence/`.
- [ ] 1.4 Audit the drill-down percentage contract: record the ListView
      column set (no `% of Volume`), the historical task 3.2 claim, and the
      canonical requirement. Evidence note in `evidence/`.
- [ ] 1.5 Stress-test `TreemapLayout` minimum-size behavior with
      deterministic fixtures (1 large + 10 medium + 100 tiny; 300 equal
      tiny; 1000 equal; 1 + 500; 8:4:2): measure overlap pairs,
      out-of-container rects, covered area. Record results in `evidence/`.
- [ ] 1.6 Audit file-tile activation: record the navigate callback behavior
      (`OpenWithDefaultApplication`), the canonical requirement (open
      containing folder with file selected), and historical task 4.6.
      Evidence note in `evidence/`.

## 2. Product fixes

- [ ] 2.1 Catch-all aggregation in `TreemapLayout.h`: deterministic
      sub-minimum threshold (`kMinRectSize / min(width, height)`), ≥2 tiny
      siblings → one synthetic "Other small items" node (combined logical
      size, item count, allocated known/total pair, `path=""`, never
      navigable), exactly-one-tiny stays individual, all per-rect clamps
      removed so Squarify tiles exactly (no overlap, full containment),
      single-node case still fills its container. Signature gains caller-
      owned synthetic-node storage.
- [ ] 2.2 `TreemapView::RebuildLayout` owns/clears the catch-all storage
      per rebuild; hit-test and click skip the synthetic node
      (`path.empty()` already guards navigation); hover shows combined
      catch-all metadata.
- [ ] 2.3 Hover metadata: shared `BuildTreemapItemMetadata` in
      `TreemapSemantics.h` (path, type, logical size, allocated size with
      unknown/partial qualifiers, % parent, % volume, calculating,
      divergence callout) used by BOTH the D2D hover lines in
      `TreemapView::Render` and the UIA FullDescription.
- [ ] 2.4 `% of Volume` column in the storage drill-down ListView, fed by
      the existing `ComputeStoragePercentage` (volume denominator =
      OS-reported used bytes of the containing volume via
      `GetDiskFreeSpaceExW`, same ground truth as the Overview); sort key
      and header bookkeeping updated (7 → 8 columns).
- [ ] 2.5 File-activation fix: `WindowShell` storage navigate callback
      navigates to the containing folder with the file selected
      (`NavigateWorkspace(parent, filename)`) and hides the storage panel;
      `StorageAnalysis` treemap callback no longer sets `currentPath_` to a
      file path. Folder behavior unchanged (treemap drills in place).

## 3. Semantic projection

- [ ] 3.1 `src/ui/include/ffui/TreemapSemantics.h`: `TreemapUiaItemInfo`
      (stable idHash, name, fullPath, isDirectory, synthetic +
      aggregateItemCount, invokable, logical/allocated sizes + known +
      partial, totalAllocated + known/total counts, calculating,
      aggregateFailed, percentOfParent, percentOfVolume, FullDescription,
      ItemStatus, normalized rect, offscreen) and `BuildTreemapItemMetadata`.
- [ ] 3.2 `TreemapUiaSurface` seam: visible/scope/generation/item count/
      item info/hit-test-by-normalized-point; `StorageAnalysis` implements
      it with a lock over the SAME `TreemapView` tree + `layout_` used by
      render/hit-test. No `TreemapNode*` crosses the seam.
- [ ] 3.3 Wire `columnView_.SetUiaTreemapSurface(&storageAnalysis_)` from
      `WindowShell` initialization; the controller queries the seam live
      (no per-event state pushing).

## 4. Provider tree

- [ ] 4.1 `TreemapUiaProvider.{h,cpp}`: Group "Storage Treemap"
      (AutomationId `Storage.Treemap`, Group ControlType,
      IItemContainerProvider) + tile providers (DataItem, Name, HelpText =
      full path, ItemType, FullDescription, ItemStatus, IsEnabled,
      IsKeyboardFocusable=false honest, BoundingRectangle, IsOffscreen,
      RuntimeId; IInvokeProvider on real tiles only, none on the synthetic
      aggregate). No Selection patterns.
- [ ] 4.2 Runtime IDs: `kUiaNsTreemap = 3`, tile
      `{3, scopeHashLo/Hi, idHashLo/Hi}`, catch-all deterministic synthetic
      identity; `UiaItemIdentityHash` for real tiles.
- [ ] 4.3 Controller integration in `ColumnViewUiaProvider`: when the seam
      reports the treemap active — root `Navigate(FirstChild/LastChild)`
      returns the Treemap group; `ElementProviderFromPoint` resolves tiles
      via the seam hit-test; `GetFocus` honest (no treemap focus). When
      inactive, the tree is unchanged. Bounded tile cache
      (`kMaxCachedTreemapTiles = 1024`, documented).
- [ ] 4.4 Stale semantics: `Resolve()` per query;
      `UIA_E_ELEMENTNOTAVAILABLE` on identity mismatch; aggregate-
      resolution exception (same identity, updated metadata/bounds stays
      valid); lifetime via `UiaOwnerState`.
- [ ] 4.5 Events: structure events on open/close/scope/rebuild/filter/
      catch-all change; Invoked on real Invoke; ItemStatus property-changed
      on `Calculating…` resolution. No paint/hover events.
- [ ] 4.6 Invoke marshaling: `WM_APP_TREEMAP_NAVIGATE` value payload
      (volumeRowId/fileIdLow/fileIdHigh + path + generation + scope hash —
      no pointers), UI-thread re-resolution, exact mouse-click navigation
      path; stale queued actions drop safely.

## 5. Geometry

- [ ] 5.1 Tile `BoundingRectangle`: cached normalized rect → viewport DIPs
      via pushed `UiaViewGeometry` → `UiaRectToScreen`; `IsOffscreen` from
      real viewport clip. No second layout implementation.
- [ ] 5.2 `ElementProviderFromPoint` parity with
      `TreemapView::HandleLButtonDown`/`HandleMouseMove` (same pure
      `HitTest` + identical conversions).

## 6. Headless tests

- [ ] 6.1 Extend `fftreemap_layout_tests`: 8:4:2, square/wide/tall
      viewports, min-size boundary, hundreds of tiny files (no overlap, no
      out-of-bounds, deterministic, hit-test center, edge handling,
      client↔normalized round-trip), catch-all membership/bytes/threshold/
      ordering/hit-test, zero-total scope.
- [ ] 6.2 `fftreemap_metadata_tests`: FullDescription/hover lines for
      normal file, normal folder, calculating, allocated unknown, partial
      aggregate, divergence, percentages (parent + volume), zero denominators.
- [ ] 6.3 `fftreemap_uia_tests` (provider, fake surface): root semantics,
      tile properties, patterns (Invoke present/absent), no Selection,
      runtime IDs (indexed/degraded/duplicate names), stale providers
      (scope/filter/rebuild/Hide/destroy), aggregate-resolution update,
      lifetime, worker-thread property reads, Invoke marshaling (no UI
      mutation off-thread), geometry at 96/non-96 DPI + offset + resize +
      offscreen, ElementProviderFromPoint parity, cache cap.
- [ ] 6.4 `ffstorage_percentage_tests` additions: volume-denominator
      percentages, zero/unknown cases.
- [ ] 6.5 Storage-analysis-level regression (headless where feasible) for
      the `% of Volume` column text and file-activation routing.
- [ ] 6.6 Performance: synthetic ~10 000-tile set — first semantic
      enumeration vs cached re-walk, cache size under cap, no N²; record
      numbers in `evidence/`.

## 7. Live E2E

- [ ] 7.1 Registry: `treemap-readability-click-precision` → Group
      `treemap`, `Isolated=$true`, `Runner=Invoke-UiaScenarioTreemap`;
      historical SKIP line removed from the shared-session block.
- [ ] 7.2 Scenario runner: fresh lifecycle, disposable fixture (8/4/2/1 MiB
      real-content files + FolderA/FolderB children + many-tiny directory),
      normal navigation, real `analyze` command, native Treemap radio,
      semantic wait for `Storage.Treemap`.
- [ ] 7.3 Assertions: discovery + tile metadata, proportionality (area
      ratios vs 8:4:2 with documented tolerance), click precision from
      BoundingRectangle centers, folder Invoke (scope changes, child
      appears, old provider stale), file Invoke (column view at containing
      folder, file selected, no launch), category filter (if
      deterministic), resize, many-tiny (no overlap, containment, center
      hit returns self), before/after filesystem manifest (non-mutation).
- [ ] 7.4 Repeatability: 10 consecutive isolated runs, plus the scenario
      before and after an unrelated scenario; record durations and any
      bounded retries with reasons.

## 8. Validation gates

- [ ] 8.1 `cmake --preset debug` + build + `ctest --preset debug
      --output-on-failure` (all pass, no Not Run).
- [ ] 8.2 `cmake --preset release-test` + build + `ctest --preset
      release-test --output-on-failure` (all pass).
- [ ] 8.3 `cmake --preset analyze` + build (warning-clean).
- [ ] 8.4 `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1
      doctor`.
- [ ] 8.5 Full `pwsh ./verify/verify.ps1 run -Capability
      windows-ui-automation-validation`: 0 unexpected FAIL; the treemap
      SKIP reason `treemap-drawn-on-d2d-surface` is gone; remaining SKIPs
      are the documented independent ones.
- [ ] 8.6 Policy/drift/OpenSpec gates: `check_source_markers.ps1`,
      `check_diagnostic_policy.ps1`, `check_doc_drift.ps1`, `openspec
      validate --all --strict --no-interactive`, `git diff --check`.
- [ ] 8.7 Performance evidence recorded (first/cached enumeration, cache
      count, treemap open/layout) in `evidence/`.

## 9. Close-out

- [ ] 9.1 Evidence directory complete (baseline, audit notes, probe
      results, performance numbers, repeatability log).
- [ ] 9.2 Commit task-owned files; push `origin/main`; verify
      `HEAD == origin/main` and clean tree; report hosted CI for the pushed
      SHA.
