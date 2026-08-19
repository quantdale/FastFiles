# Tasks — semantic-treemap-uia

Code comments cite tasks as `// semantic-treemap-uia N.x:` where the change
slug disambiguates across changes.

## 1. Baseline and audit

- [x] 1.1 Run the full `windows-ui-automation-validation` capability on the unmodified tree and record the baseline: confirm `treemap-readability-click-precision` = SKIPPED (`treemap-drawn-on-d2d-surface`) and the overall PASS/SKIP/FAIL counts — done 2169e85: recorded in `evidence/audit-notes.md`; baseline SKIP confirmed, live UIA interaction skips logged as harness expectations.
- [x] 1.2 Run the treemap scenario through the `UiaScenario=` filter and record the result — done 2169e85: grouped `treemap` isolated scenarios validated via `windows-ui-automation-validation`; filtering harness semantics recorded.
- [x] 1.3 Audit hover metadata against the canonical spec: record exactly what `TreemapView::Render` draws on hover today (label, `Size:`, `Size on disk:` — no path, no percentages) and the historical task 4.5 claim — done 2169e85: corrected hover via shared `BuildTreemapItemMetadata` (`TreemapSemantics.h`) covering path/type/logical/allocated/%parent/%volume/calculating/divergence; renderer and UIA FullDescription share the same helper (`TreemapView::BuildTileInfoLocked`/`TreemapSemantics.h:125`).
- [x] 1.4 Audit the drill-down percentage contract: record the ListView column set (no `% of Volume`), the historical task 3.2 claim, and the canonical requirement — done 2169e85: corrected via `StorageAnalysis.cpp:460,733,775,957`/`StorageAnalysis.h:188` shared `ComputeStoragePercentage` plus new `% of Volume` column (grouped coverage exercised in `tests/ui`).
- [x] 1.5 Stress-test `TreemapLayout` minimum-size behavior with deterministic fixtures (1 large + 10 medium + 100 tiny; 300 equal tiny; 1000 equal; 1 + 500; 8:4:2): measure overlap pairs, out-of-container rects, covered area — done 2169e85: corrected via band-based exact tiling (removed per-rect `kMinRectSize` clamp) and synthetic "Other small items" aggregation; evidence in `evidence/layout-stress-probe.md` and `tests/ui/test_treemap_layout.cpp` (no overlaps, full containment, hit-test invariants).
- [x] 1.6 Audit file-tile activation: record the navigate callback behavior (`OpenWithDefaultApplication`), the canonical requirement (open containing folder with file selected), and historical task 4.6 — done 2169e85: corrected via `WindowShell.cpp:937` `NavigateWorkspace(parent, filename)` (hides storage panel; `StorageAnalysis` treemap callback no longer sets `currentPath_` to a file).

## 2. Product fixes

- [x] 2.1 Catch-all aggregation in `TreemapLayout.h` — done 2169e85: deterministic `kMinRectSize / min(width,height)` threshold, ≥2 tiny → synthetic "Other small items" (combined sizes, aggregate count, allocated pair, `path=""` non-navigable), exactly-one-tiny stays individual, all per-rect clamps removed so Squarify tiles exactly (src/ui/include/ffui/TreemapLayout.h:26,136), single-node fills container; caller-owned synthetic storage.
- [x] 2.2 `TreemapView::RebuildLayout` owns/clears catch-all per rebuild — done 2169e85: hit-test/click skip synthetic (`path.empty()`), hover shows combined catch-all metadata (src/ui/src/TreemapView.cpp:57,127; StorageAnalysis.cpp:143).
- [x] 2.3 Hover metadata shared helper — done 2169e85: `BuildTreemapItemMetadata` in `TreemapSemantics.h:125` used by both D2D hover lines in `TreemapView::Render` and UIA FullDescription; never drifts (TreemapView.cpp:404,412,560).
- [x] 2.4 `% of Volume` column in storage drill-down ListView — done 2169e85: volume denominator from `GetDiskFreeSpaceExW` (same ground truth as Overview); sort key/header 7→8 columns; fed by shared `ComputeStoragePercentage` dual denominators (StorageAnalysis.cpp:460,733,775,957; StorageAnalysis.h:188).
- [x] 2.5 File-activation fix — done 2169e85: `WindowShell` storage navigate callback does `NavigateWorkspace(parent, filename)` and hides storage panel (WindowShell.cpp:937); `StorageAnalysis` treemap callback no longer sets `currentPath_` to a file path (StorageAnalysis.cpp:140-146); folder drilling unchanged.

## 3. Semantic projection

- [x] 3.1 `TreemapSemantics.h:82,125` — done 2169e85: `TreemapUiaItemInfo` + `BuildTreemapItemMetadata` (values per tile: stable idHash, name/fullPath/isDirectory, synthetic+aggregate count, invokable, logical/allocated with known/partial, totals, calculating/aggregateFailed, %parent/%volume, FullDescription/ItemStatus/normalized rect/offscreen).
- [x] 3.2 `TreemapUiaSurface` seam — done 2169e85: `TreemapSemantics.h:288` visible/scope/generation/item count/item info/hit-test-by-normalized-point; `StorageAnalysis` implements it with a lock over the same `TreemapView` tree+`layout_` (`StorageAnalysis.h:77,84,86,89,92; StorageAnalysis.cpp:143,154,164,168,188,195`); no `TreemapNode*` crosses the seam.
- [x] 3.3 Seam wiring — done 2169e85: `WindowShell` wires `columnView_.SetUiaTreemapSurface(&storageAnalysis_)` at initialization; controller queries live (ColumnView.cpp:2392, ColumnView.h:369,372,413).

## 4. Provider tree

- [x] 4.1 `TreemapUiaProvider.{h,cpp}` — done 2169e85: Group "Storage Treemap" (`Storage.Treemap`, Group, IItemContainerProvider) + tile DataItems (Name/HelpText/full path/ItemType/FullDescription/ItemStatus/IsEnabled/IsKeyboardFocusable=false honest/BoundingRectangle/IsOffscreen/RuntimeId; IInvokeProvider on real tiles only; synthetic aggregate has none; no Selection) — TreemapUiaProvider.{h,cpp}:1,46,50,62,99,109,132.
- [x] 4.2 Runtime IDs — done 2169e85: `kUiaNsTreemap=3`, tile `{3, scopeHashLo/Hi, idHashLo/Hi}`, catch-all deterministic synthetic identity; `UiaItemIdentityHash` for real tiles (UiaRuntimeIds.h:2; TreemapUiaProvider.cpp:46).
- [x] 4.3 Controller integration — done 2169e85: `ColumnViewUiaProvider` treemap-active path returns Treemap group from root `Navigate`, resolves tiles via seam hit-test, `GetFocus` honest; inactive tree unchanged; bounded tile cache `kMaxCachedTreemapTiles=1024` (ColumnViewUiaProvider.cpp:127,132,141,220,306,317,430,773; TreemapView.cpp:127).
- [x] 4.4 Stale semantics — done 2169e85: `TreemapTileUiaProvider::Resolve` per query; `UIA_E_ELEMENTNOTAVAILABLE` on identity mismatch; aggregate-resolution exception keeps valid tile with updated metadata/bounds; lifetime via `UiaOwnerState` (TreemapUiaProvider.cpp:265; UiaOwnerState/WRL ownership).
- [x] 4.5 Events — done 2169e85: structure events on open/close/scope/rebuild/filter/catch-all, Invoked on real Invoke, ItemStatus property-changed on `Calculating…` resolution; no paint/hover events (UiaNotifications.h:5; TreemapView.cpp:57,127; ColumnViewUiaProvider.cpp:773).
- [x] 4.6 Invoke marshaling — done 2169e85: `WM_APP_TREEMAP_NAVIGATE` value payload (volumeRowId/fileIdLow/fileIdHigh + path + generation + scopeHash — no pointers), UI-thread re-resolution via exact mouse-click path; stale queued actions drop safely (StorageAnalysis.h:26,29; StorageAnalysis.cpp:168,178,188,195; TreemapSemantics.h:321).

## 5. Geometry

- [x] 5.1 Tile `BoundingRectangle` — done 2169e85: cached normalized rect → viewport DIPs via pushed `UiaViewGeometry` → `UiaRectToScreen`; `IsOffscreen` from real viewport clip; no second layout impl (TreemapUiaProvider.cpp:62; UiaRuntimeIds.h:2).
- [x] 5.2 `ElementProviderFromPoint` parity — done 2169e85: same pure `HitTest` + identical conversions as `TreemapView::HandleLButtonDown`/`HandleMouseMove` (ColumnViewUiaProvider.cpp:306,317; TreemapView.cpp:404,412,481,543).

## 6. Headless tests

- [x] 6.1 `fftreemap_layout_tests` — done 2169e85: 8:4:2, square/wide/tall viewports, min-size boundary, hundreds tiny (no overlap/no out-of-bounds/deterministic/hit-test center/edge handling/client↔normalized round-trip), catch-all membership/bytes/threshold/ordering/hit-test, zero-total scope (tests/ui/test_treemap_layout.cpp).
- [x] 6.2 `fftreemap_metadata_tests` — done 2169e85: FullDescription/hover lines for normal file/folder, calculating, allocated unknown, partial aggregate, divergence, percentages (parent+volume), zero denominators (tests/ui/test_treemap_metadata.cpp).
- [x] 6.3 `fftreemap_uia_tests` — done 2169e85: provider/fake surface root semantics, tile props, Invoke present/absent, no Selection, runtime IDs, stale providers (scope/filter/rebuild/Hide/destroy), aggregate-resolution update, lifetime, worker-thread reads, Invoke marshaling (no UI mutation off-thread), geometry DPI/offset/resize/offscreen, ElementProviderFromPoint parity, cache cap (`tests/ui/test_treemap_uia.cpp`; bounded `kMaxCachedTreemapTiles=1024`).
- [x] 6.4 `ffstorage_percentage_tests` additions — done 2169e85: volume-denominator percentages, zero/unknown cases (tests/ui/test_storage_percentage.cpp).
- [x] 6.5 Storage-analysis headless regressions — done 2169e85: `% of Volume` column text + file-activation routing covered headlessly where deterministic (StorageAnalysis/ColumnView TreemapSemantics seams).
- [x] 6.6 Performance — done 2169e85: synthetic ~10k-tile enumeration first vs cached re-walk, cache under cap, no N²; numbers captured as part of layout/Uia provider tests and probe sizing (evidence/ layout-stress-probe; test expectations enforce bounds).

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
