# Design — Treemap Semantic UIA

## 1. Audit findings (evidence-backed)

The canonical spec (`openspec/specs/storage-drilldown-and-treemap/spec.md`)
and the current code were compared line by line. Four defects were confirmed;
the deterministic probes are recorded in `evidence/`.

### D1 — Hover metadata gap (spec "Treemap Hover Metadata")
Spec: hover shows full path, size, percentage of parent, percentage of
volume. Code: `TreemapView::Render` (src/ui/src/TreemapView.cpp:350-375)
draws only `Size:` and `Size on disk:` when hovered. No path, no
percentages. Historical task 4.5 ("hover metadata complete") is false.
**Fix:** shared `BuildTreemapItemMetadata` + D2D hover lines from it.

### D2 — Drill-down `% of Volume` gap (spec "Hierarchical Drill-Down with Percentage Context")
Spec: every listed item shows % of immediate parent AND % of overall volume.
Code: 7 list columns (StorageAnalysis.cpp:112), `% of Parent` only.
Historical task 3.2's "percentage-of-volume display integrated" claim is
false. **Fix:** add `% of Volume` column fed by the same
`ComputeStoragePercentage` used for `% of Parent` (one shared percentage
source; the denominator differs, the formatter does not).

### D3 — Small-item layout defect (historical task 4.3)
Task 4.3: items below the minimum-visible-size threshold use an
"other small items" catch-all. Code: `TreemapLayout.h` clamps every rect to
`max(kMinRectSize, ...)` — no catch-all. The deterministic stress probe
(tools probe, results in evidence/): 1 large + 10 medium + 100 tiny →
4950 overlapping pairs, 100 rectangles out of container, 49950% covered
area; 300 equal tiny → 3328 overlaps; 1000 equal → 38556 overlaps. Root
cause: `Squarify`'s band grouping converges to one band per container
(aspect-ratio target 1), so every rect height = share × container height;
clamping tiny heights to 4.0 stacks them past the container bottom, and the
clamped rects overlap the rects that follow (the unclamped `innerPos`/`pos`
advance). **Fix:** catch-all aggregation + exact tiling (section 8).

### D4 — File-tile activation defect (spec "Treemap Click-to-Navigate")
Spec: a file rectangle opens the standard file-browsing surface at the
containing folder with the file selected, no file operation. Code:
`WindowShell` navigate callback (WindowShell.cpp:933-949) calls
`OpenWithDefaultApplication` for files — it launches the file. Historical
task 4.6 is not satisfied. **Fix:** reuse the established
`item.open-containing-folder` path (`NavigateWorkspace(parent, filename)`)
after hiding the storage panel; also stop `StorageAnalysis`'s treemap
callback from setting `currentPath_` to a file path (StorageAnalysis.cpp:
140-146).

## 2. UIA tree shape

One fragment root only — the existing `ColumnViewUiaProvider` (Window
element, AutomationId `MainWindow`) returned from `WM_GETOBJECT`. No second
fragment root, no per-tile HWNDs, no invisible accessibility-only windows.

```
Window "FastFiles" (existing fragment root; native chrome below via the
                    host provider — storage radio buttons/ListView/combo
                    remain authoritative native HWND controls)
 └─ Group "Storage Treemap"        (AutomationId "Storage.Treemap")
     ├─ DataItem <tile>            (real files/folders, Invoke)
     ├─ DataItem <tile>
     └─ DataItem "Other small items" (synthetic aggregate, NO Invoke)
```

While the treemap is the active workspace surface, the root's
`Navigate(FirstChild/LastChild)` returns the Treemap group instead of the
obscured Column View group, and `ElementProviderFromPoint` resolves treemap
tiles. When the treemap is not active, the tree is exactly as today. The
Column View group provider still answers its own queries, but is not
reachable from the root while the treemap is the visible surface (a stale
client-held Column View element remains individually valid; it is not
retargeted). Native storage controls are never duplicated: the ListView
rows keep their native proxy providers in every list mode; the custom tree
only adds the D2D treemap surface.

## 3. Tile ControlType decision

**DataItem** (`UIA_DataItemControlTypeId`). Rationale: tiles are
non-interactive rows of a data visualization — exactly the DataItem
semantic (compare Excel chart data series / monitoring dashboards). They
are not ListItems (no list container semantics, no selection), not Buttons
(activation is a navigation affordance, and the synthetic aggregate is not
an action at all), and not Custom (they carry real properties: Name,
HelpText path, ItemType, FullDescription, bounds). Standard control type;
documented here so the choice is reviewable. The container is a **Group**
(not a List): children are not selectable rows, and Group is the standard
container for a DataItem collection.

## 4. Patterns

- Real tiles (file/folder): `IInvokeProvider` only.
- Synthetic "Other small items" aggregate: **no** Invoke pattern (it is not
  a single filesystem object and has no single navigation target).
- No Selection / SelectionItem anywhere: the treemap has no selection
  model; hover is not selection and must not be faked as one.
- `IItemContainerProvider` on the Group (FindItemByProperty by Name over
  the semantic snapshot; bounded, no provider materialization).
- Keyboard focus: the treemap has no keyboard-focus model today; tiles
  report `IsKeyboardFocusable=false` and `HasKeyboardFocus=false`. This is
  honest, not hidden-state automation. UIA clients can still Invoke tiles.
  (Adding a real focus model is out of scope and would need real keyboard
  UX, not automation-only state.)

## 5. Stable runtime identity

New namespace `kUiaNsTreemap = 3` (UiaRuntimeIds.h). Tile runtime ID:
`{3, scopeHashLo, scopeHashHi, idHashLo, idHashHi}` where `idHash` uses the
established `UiaItemIdentityHash(volumeRowId, fileIdLow, fileIdHigh, path)`
(FRN when present, else full-path hash), and `scopeHash` = hash of the
current treemap scope path. The scope component keeps IDs of the same file
under different scopes distinct and makes the whole set change on scope
navigation (the documented epoch policy). The synthetic catch-all tile uses
`idHash` = hash of `scopePath + "\\.ff-other-small-items"` — deterministic,
never colliding with a real path identity. Layout position and vector index
are never used as identity. Duplicate names in different paths get
different IDs (path/FRN hashing); identical files in the same scope are
distinct entries with distinct FRNs or distinct full paths.

## 6. Provider lifetime

Reuses `UiaOwnerState` exactly like Column/Home/Search: `ColumnView` owns
the shared token; `ReleaseUiaProvider()` (WM_DESTROY path) flips the token
dead before dropping app references. `TreemapGroupUiaProvider` /
`TreemapTileUiaProvider` are held by the controller (`ColumnViewUiaProvider`)
with the same cache/owner pattern; they take the owner's state pointer in
their constructor. A client that outlives the window gets clean
`UIA_E_ELEMENTNOTAVAILABLE`, never use-after-free. No COM cycle: children
hold raw `ColumnViewUiaProvider*`, never AddRef the controller.

## 7. Stale-provider semantics

Every tile provider resolves its identity against the CURRENT semantic
snapshot on each query (`Resolve(...)`); mismatch or absence →
`UIA_E_ELEMENTNOTAVAILABLE`. Never retargets the new occupant of the same
index. Stale causes: scope navigation, category-filter change, snapshot
rebuild, treemap Hide, window destruction.

**Aggregate-resolution exception (documented):** a tile whose identity is
unchanged but whose size/aggregate/layout resolved (async folder aggregate
completion) stays VALID and reports the updated metadata and bounds. The
provider caches the tile's `index` in the current layout and the layout's
`generation`; a property query re-resolves `index → info` and accepts the
result when the semantic identity hash still matches. Only identity
mismatch (or a generation change that replaced the tile set) fails.

## 8. Small-item / catch-all behavior (product fix D3)

Redesigned layout algorithm in `TreemapLayout.h` (pure header):

1. Compute `total = Σ totalSizeBytes` (min 1 for the ratio math).
2. Threshold: `minShare = kMinRectSize / min(width, height)` (normalized
   space). A node is "tiny" when `size/total < minShare` (or size == 0).
3. If ≥ 2 nodes are tiny, aggregate them into ONE synthetic catch-all node:
   name `Other small items`, `synthetic=true`, `aggregateItemCount=N`,
   `totalSizeBytes` = Σ member sizes, allocated pair = Σ known counts and
   Σ total counts (member files count as 1 each when their allocated size is
   known), `path = ""` (never navigable), `isDirectory=false`, `calculating`
   = any member calculating. Exactly-one-tiny case: the node stays
   individual and renders at its true (sub-minimum) size — honest geometry,
   no overlap, hit-test still resolves it.
4. `Squarify` drops ALL per-rect `max(kMinRectSize, ...)` clamps: band
   math then tiles exactly (Σ slice sizes = width, Σ stacked rects =
   height), guaranteeing zero overlap and full containment by construction.
   Real nodes are ≥ threshold by construction so no clamped visibility is
   lost; the single-node case still fills its container (a lone rect cannot
   overlap anything).
5. Deterministic: nodes are sorted by `totalSizeBytes` descending before
   layout (existing sort), the aggregation pass preserves sort order, and
   the catch-all participates in the sort by its combined size. A zero-total
   scope aggregates ALL nodes into one catch-all (combined 0 B, count N).

Renderer and hit-test consume the same resulting `layout_`; the catch-all
is hoverable (shows combined metadata: count, combined logical size,
combined allocated qualifier) and clickable only as a hover target —
`HandleLButtonDown` skips it (`path.empty()`), and the UIA tile exposes no
Invoke. Area remains logical-size driven (allocated sizes never change
rectangles).

## 9. Semantic projection (the seam)

`src/ui/include/ffui/TreemapSemantics.h`:

- `TreemapUiaItemInfo` — plain value per tile: stable `idHash`, name,
  fullPath, isDirectory, synthetic flag + aggregateItemCount, invokable,
  logical size + known flag, allocated size + known flag + partial flag,
  totalAllocatedSizeBytes + known/total counts, calculating, aggregateFailed,
  percentOfParent, percentOfVolume (doubles + display strings),
  FullDescription, ItemStatus, normalized rect (x/y/width/height in 0..100
  space), offscreen (derived).
- `TreemapUiaSurface` — the seam interface: visible/scope/generation/item
  count/item info/hit-test-by-normalized-point. `StorageAnalysis` implements
  it with a lock (`std::mutex`) over the SAME `TreemapView` tree + layout
  used by Render/hit-test. Provider code never dereferences `TreemapNode*`
  or `TreemapRect` and never touches the D2D layer; the provider layer
  consumes only `TreemapUiaItemInfo` values. No parallel data model — the
  seam reads the live state through one lock.

Hit-test parity: the seam exposes `TreemapUiaHitTest(nx, ny)` implemented as
`HitTest(layout_, nx, ny)` — the exact pure function `TreemapView` uses
after its client→normalized conversion. The provider converts
screen→client→DIP→normalized using the pushed `UiaViewGeometry` (the same
geometry the shell pushes for ColumnView; the treemap renders under the
same Translate + viewport scale), then calls the seam. A pure parity test
walks deterministic points and asserts
`layout hit-test == provider ElementProviderFromPoint tile`.

## 10. Geometry ownership

`BoundingRectangle` = the cached `layout_` rect, converted:
normalized rect → viewport DIPs (`x/100*widthDip`, `y/100*heightDip`) using
the pushed `UiaViewGeometry` (`widthDip`/`heightDip` = the exact
`detailsLayout.availableColumnWidthDip`/`viewportHeightDip` the treemap
renders into) → physical screen pixels via the existing
`UiaRectToScreen(rectDip, geometry)` (dpiScale + origin + ClientToScreen).
No layout math is reimplemented in the provider; no second geometry
implementation exists. `IsOffscreen` = no intersection with the treemap
viewport clip (mirror of `UiaRectOffscreen` for the treemap surface).

## 11. Threading boundary

UIA property queries can arrive on a UIAutomationCore worker thread
(observed in stabilize-live-uia-e2e). All provider reads go through the
seam's bounded lock (released before any UIA event call), take no other
locks, do no filesystem I/O, no engine request, no SendMessage, no
unbounded wait.

**Invoke marshaling (Search precedent):** provider `Invoke()` validates the
tile identity in the current snapshot, then checks
`GetCurrentThreadId() != GetWindowThreadProcessId(hwnd)`; when off-thread it
`PostMessageW(owner, WM_APP_TREEMAP_NAVIGATE, ...)` carrying a VALUE payload
(volumeRowId/fileIdLow/fileIdHigh + full path + layout generation + scope
hash — never a pointer), and returns. WindowShell handles the message on
the UI thread, re-resolves the identity in CURRENT state, and runs the
exact same navigation mutation as a mouse click
(`StorageAnalysis`/`TreemapView` `HandleLButtonDown` path). Stale queued
actions (scope changed / rebuild / Hide / destroy since the post) resolve
to nothing or fail safely; they can never retarget a new tile. When the
call already arrives on the UI thread, Invoke runs inline (same re-validation).

## 12. Metadata source

`BuildTreemapItemMetadata(node, parentTotal, parentKnown, volumeUsedBytes,
scopePath)` in `TreemapSemantics.h` produces the canonical strings:

- FullDescription (multiline): full path, type, logical size, allocated
  size with unknown/partial qualifiers, `% of parent`, `% of volume`,
  calculating/partial state, divergence callout.
- HoverLines: the exact lines the D2D hover paints.
- ItemStatus: `Calculating…` / `Partial` / empty.

Both the renderer (TreemapView::Render) and the UIA tile provider consume
this one function — text parity is by construction, proven by unit tests
and the live scenario (no OCR).

Percentage source: `ComputeStoragePercentage(rowSize, denominator,
known)` (existing, overflow-safe divide-before-multiply) for BOTH the
`% of Parent` column and the new `% of Volume` column in the drill-down
list, and for the treemap tile metadata. `% of parent` denominator =
`CurrentParentTotals()` (the same as the list). `% of volume` denominator =
OS-reported used bytes of the containing volume (`GetDiskFreeSpaceExW`,
the same ground truth the storage Overview uses; spec: "percentage-of-
volume SHALL continue to reflect the total size of the whole volume").
One percentage implementation, two denominators, cached per scope.

## 13. Calculating / partial semantics

- A `calculating` tile stays present, ItemStatus `Calculating…`, logical
  size never rendered as 0 B (unknown affordance).
- Allocated unknown → "Size on disk: Unknown" / `—`-equivalent in metadata,
  never 0 B.
- Partial directory aggregate → `≥ N B (k/total)` qualifier, never an
  exact total.
- Divergence callout: when both known and `max(logical, allocated) ≥
  min × 1.5`, FullDescription/hover state "differs from logical size".
- Degraded/partial index coverage: the storage status line remains the
  coverage authority; treemap metadata exposes the current scope honestly
  and never claims whole-volume completeness. No privileged service is
  needed for core treemap UIA.

## 14. Category filter

Filter changes rebuild the tree (existing `SetCategoryFilter` →
`BuildTree` → `RebuildLayout`). UIA behavior: the item set reflects the
filtered layout; percentages use the filtered displayed scope exactly as
the product already does (existing rule); structure-changed event fires on
the Group; pre-filter tile providers fail cleanly (identity gone); runtime
IDs of surviving semantic identities stay valid under the documented epoch
policy (identity hash unchanged, scope unchanged); no filtered-out file is
reachable through the tree (the snapshot excludes it).

## 15. Provider caching limits

On-demand creation, keyed cache on the controller, hard cap
`kMaxCachedTreemapTiles = 1024` (documented; overflow clears the tile cache
deterministically, like Column's 4096-item cap). No provider allocation
during paint; no provider work when no client is attached (the controller
is created lazily on WM_GETOBJECT and the seam is read-only). The Group
implements `IItemContainerProvider` so clients can locate tiles without
enumerating thousands of children. A synthetic 10 000-tile enumeration
benchmark (headless) measures first-walk vs cached-walk time and the cache
size under the cap.

## 16. UIA event policy

Semantic events only, through the existing `UiaEventRouter`-style
notification → `UiaRaiseAutomationEvent` architecture (the treemap surface
gets a small router; the controller registers). Events:
- StructureChanged (ChildrenBulkAdded/Removed/Reordered) on the Group for:
  treemap opened, treemap closed, scope navigation, snapshot rebuild,
  category-filter change, catch-all composition change.
- Invoked on a tile after a successful UIA Invoke (real navigation).
No events on paint, animation frames, or hover changes (hover is not
selection). Aggregate completion may change metadata/bounds — providers
return updated values on query (documented exception, section 7); a
property-changed event is raised for ItemStatus when a `Calculating…` tile
resolves (bounded, semantic).

## 17. No-pixel validation strategy

Everything is validated semantically:
- Tile geometry comes from provider `BoundingRectangle` (physical screen
  pixels derived from the cached layout + real window transform).
- Click precision: mouse input only at coordinates derived from
  `BoundingRectangle` centers; the result is verified via the real
  navigation state (new scope / selected file), never by screenshots.
- Text parity: shared `BuildTreemapItemMetadata` + unit tests + live
  FullDescription assertions. No OCR, no image matching, no fixed screen
  coordinates, no test-only IPC or hidden hooks.

## 18. Live harness

`treemap-readability-click-precision` becomes an ISOLATED scenario
(`Isolated=$true`, `Runner=Invoke-UiaScenarioTreemap`, Group `treemap`):
fresh FastFiles lifecycle, disposable fixture (8/4/2/1 MiB files +
FolderA/FolderB + a many-tiny directory), normal UI navigation to the
fixture, real product command to open Storage Analysis, native Treemap
radio click, semantic wait for `Storage.Treemap`, then assertions:
discovery, proportionality (area ratios ≈ logical ratios within layout
tolerances), BoundingRectangle click precision, folder Invoke, file Invoke
(column view opens at containing folder, file selected, manifest
unchanged), category filter (if deterministic), resize, many-tiny no-overlap
(UIA geometry), and a before/after filesystem manifest for non-mutation.
Repeatability: 10 consecutive runs, plus before/after an unrelated scenario.

## 19. Non-destructive guarantee

Provider reads are read-only. Invoke is navigation-only. The live scenario
takes a before/after manifest (names, sizes, mtimes) of the fixture and
asserts equality; only harness fixture creation/removal touches the disk.

## 20. Historical-evidence truthfulness

Historical storage-analysis tasks 3.2, 4.3, 4.5, 4.6 were recorded complete
but their claims do not match the current code. This change does NOT
rewrite history: `evidence/` records what each task claimed, what the
semantic audit exposed (with code references and deterministic probe
results), and the corrective change + new validation for each.
