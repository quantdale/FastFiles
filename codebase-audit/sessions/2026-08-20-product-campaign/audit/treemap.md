# Audit area: STORAGE ANALYSIS &amp; TREEMAP — audit src/ui/src/StorageAnalysis.{h,cpp}, TreemapView.{h,cpp}, TreemapFilter.h, src/ui/include/ffui/TreemapLayout.h, TreemapSemantics.h, StoragePercentage.h, StorageVisibility.h. Cover: folder aggregation math, % of Parent and % of Volume consistency (sums, rounding), zero-byte/tiny/huge items, UNKNOWN allocated-size representation (must never fabricate a meaningful 0), drill-down navigation state, sorting, treemap interaction hit-testing, label truncation/overlap at small rects, degraded-mode analysis behavior, huge synthetic sizes/overflow.

(agent agent-5, outcome completed)

## Defects

### [P1] Treemap build recursively walks the entire indexed subtree and fires one async aggregate request per directory — TreemapView.cpp:263–330
- Symptom (user-visible): selecting the Treemap view at a volume root (`C:\`) stalls the UI thread, balloons memory, and floods the engine with aggregate requests; results then trickle in retileing the map for seconds/minutes.
- Evidence: `populate` recurses through **every** descendant directory in the snapshot (`TreemapView.cpp:263–306`) creating a `TreemapNode` per entry, and `requestAggregate(*child)` is invoked for **every directory at every depth** (`TreemapView.cpp:293`, `327`). The whole build runs under `uiaMutex_` inside `ShowAndFocus`/`OnSnapshotUpdated` (`TreemapView.cpp:50–56`, `141–145`), i.e. on the UI thread. Each completion posts a `WM_APP_TREEMAP_AGGREGATE` payload (`TreemapView.cpp:252`).
- Failure scenario: index covers a 1M-directory volume; user clicks "Treemap" while scoped to `C:\`. UI thread blocks building ~millions of nodes; engine receives ~1M `RequestFolderAggregate` calls; the window queue fills with posted payloads.
- Root cause hypothesis: no depth/breadth cap and no request budget; the design only works for shallow scopes.
- Suggested fix: lay out only depth-1 children initially (lazily expand on drill-in), and/or cap concurrent aggregate requests (e.g. top-N by partial size), moving the build off the UI thread or chunking it.
- Regression test idea: headless test feeding a snapshot with a 10k-deep/wide fixture asserting bounded node count and bounded aggregate-request count per build.

### [P2] Failed folder aggregates render as perpetual "Calculating…" in the treemap, never "Partial" — TreemapView.cpp:465–473, TreemapSemantics.h:144
- Symptom: a directory whose aggregate fails (access denied, engine miss) shows "Calculating…" forever with size "Calculating…", even though the code knows the answer is "Partial"; the UIA event meanwhile announces the new status as `"Partial"` (`TreemapView.cpp:473`) which the projected metadata contradicts.
- Evidence: failure path sets `node->calculating = true; node->aggregateFailed = true;` (`TreemapView.cpp:470–472`). But `BuildTreemapItemMetadata` computes `info.aggregateFailed = node.aggregateFailed && !node.calculating` (`TreemapSemantics.h:144`) and status prefers `calculating` (`TreemapSemantics.h:189–191`), so `itemStatus = "Calculating…"` and `sizeLine` = `"Size: Calculating…"` (totalSizeBytes was zeroed at `TreemapView.cpp:466`). Same for filtered trees: `FilterTreemapNode` sets both flags (`TreemapFilter.h:45–47`) and, with a category filter active, no retry ever occurs (`requestAggregate` bails when `categoryFilterActive`, `TreemapView.cpp:229`; completions are dropped, `TreemapView.cpp:438`).
- Root cause: the failure path conflates "unknown yet" (`calculating`) with "known-partial" (`aggregateFailed`).
- Suggested fix: on failure set `calculating=false, aggregateFailed=true` so metadata yields "Partial"/"≥ X"; keep `calculating` only for genuinely pending state.
- Regression test idea: pure test on `BuildTreemapItemMetadata` with a failed-aggregate node asserting itemStatus=="Partial" and a ≥-qualified size line.

### [P2] ByCategory rows show "0 B" in the Size column and sort as zero — StorageAnalysis.cpp:565–576, 937
- Symptom: the "By Category" list shows a correct Subtree Size but "0 B" in the Size column for every category row; sorting by Size makes the order arbitrary/all-equal.
- Evidence: the aggregation branch sets only `catItem.totalSizeBytes += item.sizeBytes` (`StorageAnalysis.cpp:574`); `sizeBytes` stays 0 and `isDirectory=false`, so `LVN_GETDISPINFO` case 2 renders `FormatSize(item.sizeBytes)` = `"0 B"` (`StorageAnalysis.cpp:937`), and sort key case 2 uses `item.sizeBytes` (`StorageAnalysis.cpp:765`).
- Failure scenario: open Storage → By Category on any indexed folder.
- Root cause: category rollup populates the wrong field for the Size column semantics.
- Suggested fix: either set `catItem.sizeBytes = catItem.totalSizeBytes` after the loop, or render "—" for synthesized category rows (e.g. flag them) and make sort key 2 use `totalSizeBytes` for them.
- Regression test idea: headless test of the ByCategory rollup asserting displayed Size equals the category total.

### [P2] Clicking a file tile in the treemap corrupts the storage panel's scope — StorageAnalysis.cpp:156–162, TreemapView.cpp:531–534
- Symptom: after clicking a *file* tile, the panel's `currentPath_` is a file path; switching to Drill Down shows "Folder not yet indexed", and Up/refresh operate from the wrong anchor until the user navigates manually.
- Evidence: `NavigateFromTreemap` unconditionally does `currentPath_ = path;` before checking `isDirectory` (`StorageAnalysis.cpp:157`); `HandleLButtonDown` invokes it for any non-empty-path node including files (`TreemapView.cpp:531–534`). `RefreshData` then does `snapshot->find(file_path)` which misses (`StorageAnalysis.cpp:494–499`).
- Root cause: scope mutation performed for non-directory activations.
- Suggested fix: only assign `currentPath_` when `isDirectory`.
- Regression test idea: unit-test `NavigateFromTreemap` with a file path asserting `currentPath_` unchanged.

### [P2] Category filter claims "% recalculated relative to the filtered scope" but the parent denominator stays unfiltered — StorageAnalysis.cpp:250–262, 655–676
- Symptom: with a category filter active in Drill Down/Largest views, `% of Parent` values are computed against the *unfiltered* folder total, so visible rows sum to far less than 100% and the column disagrees with the treemap (which uses the filtered root total, `TreemapView.cpp:564`).
- Evidence: `ApplyCategoryFilter` erases files but never recomputes `parentAggregateTotal_`; `CurrentParentTotals` returns the engine's unfiltered aggregate whenever `parentAggregateKnown_` (`StorageAnalysis.cpp:656–658`). The function's own comment says "recalculating percentages relative to the filtered scope" (`StorageAnalysis.cpp:251–252`) — spec/doc claim contradicted by code.
- Suggested fix: when `activeCategoryId_` non-empty, derive the denominator from the filtered `items_` sum instead of the engine aggregate.
- Regression test idea: filtered listing test asserting `% of Parent` sums to ≈100% across retained rows.

### [P3] Deferred-refresh timer is dead code — nothing ever calls SetTimer(kRefreshTimer) — StorageAnalysis.cpp:30, 1043–1048
- Evidence: `kRefreshTimer` is killed in `Hide()` and handled in `HandleTimer`, but no `SetTimer(owner_, kRefreshTimer, kRefreshDelayMs, …)` exists anywhere in `src/ui` (grep confirms only SearchPanel/WindowShell timers). Whatever was meant to be debounced by `kRefreshDelayMs` (StorageAnalysis.cpp:31) never runs.
- Impact: latent dead affordance; any future caller expectation of a delayed refresh silently no-ops. Low user visibility today because `OnSnapshotUpdated` refreshes directly.
- Suggested fix: delete the timer plumbing or wire it where intended.

### [P3] Known-zero items show "—" instead of "0.0%" for % of Parent / % of Volume — StoragePercentage.h:23
- Evidence: `ComputeStoragePercentage` returns unknown when `rowSizeBytes == 0` (`StoragePercentage.h:23`), yet a resolved empty folder has a *known* exact 0% share. The codebase elsewhere insists zero must be presented honestly (e.g. "exactly zero on disk", `StorageAnalysis.cpp:994`), so this is an inconsistency, not a fabrication guard.
- Suggested fix: distinguish "size known and zero" from "size unknown" (pass a `rowKnown` that survives zero) and render `0.0%`.

### [P3] First-time unavailable volume is labeled "(stale)" despite never having had figures — StorageAnalysis.cpp:387–397, 424
- Evidence: when `GetDiskFreeSpaceExW` fails on the first pass there is no prior row, so `totalBytes==0` and the name still gets `kStaleSuffix` ("(stale)", line 424) while all figures show "—". "Stale" implies prior data exists.
- Suggested fix: label rows with no prior data "(unavailable)" instead of "(stale)".

### [P3] Context menu positioned at raw lParam coordinates; keyboard-invoked menu lands offscreen — StorageAnalysis.cpp:1095–1096
- Evidence: `TrackPopupMenuEx(..., GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), ...)` without handling `lParam == -1` (Shift+F10 / menu key convention). With `-1` the menu appears at (-1,-1), partially offscreen.
- Suggested fix: on `lParam == -1`, use the focused item's rect or `GetCursorPos`/center of the window.

### [P3] Dead/inconsistent synthetic catch-all failure derivation — TreemapLayout.h:119–120
- Evidence: `synthetic->aggregateFailed = !incomplete && std::any_of(... aggregateFailed ...)` is always `false` because `incomplete` already ORs every member's `aggregateFailed` (line 102). Harmless today (the `calculating` flag carries the state) but the dead expression invites misuse.
- Suggested fix: drop the redundant `any_of` or fold it into `incomplete` explicitly.

## Verified sound

- **UNKNOWN allocated size**: never fabricated as 0 B — files show "—", directories show "0 B" only when `allocatedTotalCount>0` is impossible (empty subtree), "≥ X (k/n)" for partials (`StorageAnalysis.cpp:990–1006`, `TreemapSemantics.h:209–227`).
- **Overflow handling**: parent-total overflow returns `{0,false}` rather than wrapping (`StorageAnalysis.cpp:668–673`); percentage math divides before multiplying and clamps to 1.0 (`StoragePercentage.h:27–28`); treemap accumulation saturates (`TreemapFilter.h:36–38`, `TreemapLayout.h:97–99`).
- **Stale async results**: monotonic request IDs + `pending_.clear()` on refresh; late parent/item results dropped (`StorageAnalysis.cpp:467–476`, `684–704`); treemap payloads carry generation+path+lifetime token and are dropped on mismatch (`TreemapView.cpp:235–255`, `437`).
- **UIA Invoke marshaling**: worker-thread Invokes post a value-only payload, re-validated against current generation/scope/identity on the UI thread (`StorageAnalysis.cpp:168–207`).
- **Squarify tiling**: band math tiles the container exactly (shares sum to 1 via effective sizes incl. placeholder), no clamping regressions; hit-test inverse transform matches the render transform (`TreemapLayout.h:215–299`, `313–332`).
- **Degraded mode**: overview uses OS-only capacity; degraded snapshots compute aggregates from browsed snapshot data instead of perpetual "Calculating", with partial coverage marked (`StorageAnalysis.cpp:352–358`, `531–546`); stale volumes retained and drill-down blocked (`StorageAnalysis.cpp:903–916`).
- **Label overlap**: text drawn only above 40×20 px tiles, draw-rect inset shrinks on tiny tiles, radius clamped (`TreemapView.cpp:378–401`).
- **Mode visibility policy** matches the enum-driven table in `StorageVisibility.h`; Hide tears down treemap state and emits structure-changed events post-lock.
