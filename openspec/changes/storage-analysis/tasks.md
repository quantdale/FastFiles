## 1. Foundations & Cross-Capability Contracts

- [ ] 1.1 Document the read-side contract this change assumes from `filesystem-index-store`: per-node logical size, allocated (on-disk) size, aggregate-known/pending status, and parent/hierarchy references — as a coordination note for `index-storage-and-scanning`, not a spec change owned here — **contract design required**; the index projection already exposes `sizeBytes`, `parentFrn`, and `ChildIndices`, but aggregate-known/pending status and allocated-size are not yet modeled.
- [ ] 1.2 Define the async "size not yet known" request/subscribe pattern (immediate "pending" response, later "resolved" notification) shared by all storage views — **protocol message definitions required**; `RequestFolderAggregate`/`FolderAggregateResult` are the draft contract pending implementation in `file-preview-and-properties` §6.
- [ ] 1.3 Define the on-disk, user-editable schema for the extension-to-category mapping (category id, display name, ordered extension pattern list, "Other/Uncategorized" fallback) and decide its storage location — **`Settings::storageCategories` exists** as a vector of (name, extensions) pairs; the richer schema and persistence format remain.
- [ ] 1.4 Author the shipped default mapping covering video, image, document, archive, executable, development-files, VM-images, games, and other representative categories — **blocked on 1.3**.

## 2. Storage Overview

- [ ] 2.1 Implement fixed local volume enumeration for storage overview, independent of the filesystem index — **volume enumeration exists** in `EngineClient`/`GetLogicalDrives`; storage-overview-specific UI remains.
- [ ] 2.2 Implement total/used/free/percentage retrieval per volume via the operating system's own reporting (`GetDiskFreeSpaceEx`) — **OS API available**; storage-overview integration remains.
- [ ] 2.3 Implement the per-volume indexed-coverage indicator (fully indexed vs. browsed/pinned-only degraded coverage) — **index health derivation exists** (`DeriveIndexHealth`); storage-overview coverage indicator UI remains.
- [ ] 2.4 Implement stale/unavailable volume handling: a disconnected volume retains last-known capacity figures, clearly labeled as stale, with live drill-down disabled until reconnection — **unavailable-volume tracking exists** (`SetVolumeAvailable`, `ForgetUnavailableVolume`); storage-overview stale rendering remains.
- [ ] 2.5 Implement drive selection that opens `storage-drilldown-and-treemap` scoped to the selected volume's root — **blocked on 3.1** (drill-down view).
- [ ] 2.6 Verify storage overview renders correct capacity figures with the privileged service disconnected (degraded mode) — **blocked on 2.1-2.4**; `GetDiskFreeSpaceEx` works without the service.

## 3. Hierarchical Drill-Down

- [ ] 3.1 Implement the drill-down list view consuming pre-aggregated sizes from the index projection, with the async "Calculating…" fallback for pending nodes — **folder aggregate contract required** (blocked on `file-preview-and-properties` §6); `ChildIndices` + `entries_` provide the data once aggregates are available.
- [ ] 3.2 Implement percentage-of-parent and percentage-of-volume computation at every drill-down level, recalculated as the user descends — **blocked on 3.1**.
- [ ] 3.3 Implement in-place update of size and dependent percentages when a pending "Calculating…" value resolves, without requiring manual refresh — **blocked on 3.1 + folder aggregate async pattern**.
- [ ] 3.4 Implement the largest-folders view (descending size rank) for a volume or chosen subtree scope — **blocked on 3.1**.
- [ ] 3.5 Implement the largest-files view (descending size rank) for a volume or chosen subtree scope — **blocked on 3.1**.
- [ ] 3.6 Implement sorting by size, name, type, and last-modified date, ascending/descending, across drill-down and largest-files/-folders listings — **sort infrastructure exists** (`ffsearch::SortResults`); storage-analysis-specific sort UI remains.
- [ ] 3.7 Implement partial-coverage labeling in the drill-down view when index coverage is limited to browsed/pinned directories (degraded mode) — **index health and coverage indicators exist**; drill-down labeling UI remains.

## 4. Treemap Visualization

- [ ] 4.1 Implement the squarified treemap layout algorithm operating on pre-aggregated size data, with no filesystem access at layout time — **blocked on 3.1** (aggregate data).
- [ ] 4.2 Implement layout-tree caching, invalidated only on data change or viewport resize, never per-frame — **blocked on 4.1**.
- [ ] 4.3 Implement Direct2D rendering of the treemap: nested rectangles, a minimum-visible-size threshold with an "other small items" catch-all rectangle below it, and labels where space permits — **blocked on 4.1**.
- [ ] 4.4 Implement recursive bounds-based hit-testing over the cached layout tree for hover and click — **blocked on 4.1**.
- [ ] 4.5 Implement hover metadata display (full path, size, percentage of parent, percentage of volume), updating as the pointer moves between rectangles — **blocked on 4.1**.
- [ ] 4.6 Implement click-to-navigate: a folder rectangle drills the treemap/drill-down into that folder; a file rectangle opens `column-view-browsing` at that file's containing folder with the file selected — **navigation handoff exists**; treemap click integration remains.
- [ ] 4.7 Implement the "Calculating…" visual affordance for treemap rectangles whose size isn't yet known, visually distinct from a resolved or zero-size rectangle — **blocked on 3.1 + async pattern**.
- [ ] 4.8 Implement logical-size-driven treemap area by default, with allocated (on-disk) size surfaced in hover/detail metadata and called out when it diverges meaningfully from logical size — **allocated size not yet in projection**; remains.

## 5. File-Type Categorization

- [ ] 5.1 Implement the extension-matching engine: case-insensitive, most-specific-pattern-wins, explicit "Other/Uncategorized" fallback for unmatched or missing extensions — **pattern matching exists in `ffsearch::Query`**; storage-analysis category engine remains.
- [ ] 5.2 Implement loading and persisting the user-editable mapping (shipped defaults plus any user customization), surviving application restarts — **`Settings::storageCategories` exists**; richer schema persistence remains (blocked on 1.3).
- [ ] 5.3 Implement the breakdown-by-category view: aggregate size and item count per category within a selected volume or folder scope — **blocked on 3.1 + 5.1**.
- [ ] 5.4 Implement category-filtering integration with drill-down/treemap views, scoping displayed items to a selected category and recalculating percentages relative to the filtered scope — **blocked on 3.1 + 5.1**.
- [ ] 5.5 Expose the mapping's edit surface as a documented extension point for `settings-and-appearance` to build its editing UI against (coordination task, not the settings UI itself) — **blocked on 1.3 + 5.2**.

## 6. Non-Destructive Guarantee & Navigation Handoff

- [ ] 6.1 Audit every interactive affordance in `storage-overview`, `storage-drilldown-and-treemap`, and `file-type-categorization` to confirm none directly creates, deletes, moves, renames, or modifies a file or folder — **design guarantee**; audit pending implementation.
- [ ] 6.2 Implement delete/move context actions from drill-down/treemap items as hand-offs into `file-operations-core`'s own confirmation and execution flow, never a direct call — **file-operations handlers exist**; storage-analysis handoff wiring remains.
- [ ] 6.3 Implement navigation hand-off into `column-view-browsing`/`navigation-and-workspace` for click-to-navigate targets — **navigation handoff exists**; storage-analysis-specific wiring remains.

## 7. Validation

- [ ] 7.1 Test instant-open behavior: opening storage views for an already-fully-indexed large volume shows aggregate sizes with no perceptible traversal delay — **blocked on 3.1**.
- [ ] 7.2 Test the mid-scan "Calculating…" path: open storage views during initial indexing and confirm pending sizes resolve in place without blocking navigation — **blocked on 3.1 + async pattern**.
- [ ] 7.3 Test degraded-mode behavior end-to-end: no privileged service connected, storage overview still shows correct capacity, drill-down/treemap correctly scoped and labeled as partial — **blocked on 2.1-2.4 + 3.1**.
- [ ] 7.4 Test treemap readability and click precision against a directory with many small files (aspect-ratio and hit-testing correctness) — **blocked on 4.1-4.4**.
- [ ] 7.5 Test, including via static/automated audit of API calls used, that no storage-analysis code path performs a filesystem mutation — **static audit can run** once code exists; currently no storage-analysis code paths exist.
