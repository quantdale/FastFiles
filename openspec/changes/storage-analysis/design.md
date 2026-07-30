## Context

`establish-architecture-foundation` locked in the three-process architecture (`FastFilesIndexSvc` / `FastFilesEngine` / `FastFiles`) and made degraded mode (no privileged service) a permanent, first-class operating state. `index-storage-and-scanning` builds on that to give `FastFilesEngine` a durable SQLite store plus a compact in-memory projection covering size, hierarchy, and timestamps for the whole indexed volume(s), incrementally maintained rather than rebuilt by full rescans.

`storage-analysis` is the first capability set that turns that index into a human-facing "what's eating my disk" experience — a WizTree-style volume/folder size breakdown and a Direct2D-rendered treemap — plus a configurable file-type/category breakdown. It is a purely additive, read-only consumer of the index: it does not own any durable state of its own beyond a small user-editable category-mapping configuration, and it hands off both "go look at this location" (to `column-view-browsing`/`navigation-and-workspace`) and "do something destructive with this item" (to `file-operations-core`) rather than implementing either itself.

Because `index-storage-and-scanning` has only a `proposal.md` at the time of writing (its own `design.md`/`specs`/`tasks.md` are not yet authored), this design treats the "bottom-up aggregated size, incrementally maintained" contract as an assumed dependency and flags the exact schema/API shape as a coordination point for that change, rather than speculatively modifying a spec that doesn't exist yet in this repo. This change's own proposal is explicit that it modifies no existing specs; nothing here should be read as silently amending `filesystem-index-store`.

## Goals / Non-Goals

**Goals:**
- Make opening the storage view feel instant for already-indexed data — no full-tree traversal at view-open time.
- Degrade honestly: when a size isn't known yet (mid-scan) or coverage is partial (degraded mode, browsed/pinned-only), say so explicitly rather than blocking, guessing, or silently showing a wrong number.
- Render a readable, precisely clickable treemap (squarified layout) as a custom Direct2D surface, with hover metadata and click-to-navigate into the normal browsing experience.
- Provide a file-type/category breakdown driven by a mapping that is data, not code, so it can be extended and user-edited without a release.
- Guarantee, structurally, that no code path in this capability set can mutate the filesystem.

**Non-Goals:**
- Implementing or modifying the size-aggregation maintenance logic inside `filesystem-index-store` itself — that lives in `index-storage-and-scanning`. This change specifies what storage-analysis reads and how it behaves when data isn't ready, not how the index computes or persists it.
- Content-based file-type sniffing (magic bytes, MIME detection) — categorization here is extension-based only.
- Any delete/move/rename/create action — those belong exclusively to `file-operations-core`.
- A settings/editing UI for the category mapping — that UI is `settings-and-appearance`'s to build; this change only defines the mapping's data shape and behavior as the extension point that UI will target.

## Decisions

### D1: Sizes are pre-aggregated bottom-up and maintained incrementally, not recomputed per view-open

Folder size (and volume-level rollups) are computed as the sum of descendant sizes, maintained incrementally by `filesystem-index-store` as the index itself updates (create/delete/rename/write events adjust ancestor aggregates by delta), and simply *read* by storage-analysis when a view opens.

**Why:** the entire value proposition of this feature is an instant answer. A multi-terabyte, millions-of-files volume cannot be walked in the time it takes a view to open — that would reproduce exactly the problem WizTree/TreeSize solve by reading the MFT once and maintaining aggregates, rather than doing a live directory walk on every launch. Since FastFiles already builds and incrementally maintains a persistent index for search, reusing that same incremental-update machinery for size aggregates avoids standing up a second, parallel "walk and sum" system.

**Alternatives considered:**
1. *Recompute per view-open by walking the in-memory hierarchy.* Rejected — still O(n) over the subtree; on a large volume this is perceptibly slow, defeating the "instant" goal even though it avoids disk I/O.
2. *Compute on demand directly against the filesystem (WizTree/TreeSize's on-demand full-MFT-scan pattern).* Rejected as the primary path — slower than reading an already-maintained aggregate, and redundant given FastFiles already keeps a live index specifically to avoid repeated full scans.
3. *Periodic batch recompute (e.g., every few minutes) instead of event-driven incremental update.* Rejected — reintroduces staleness after a burst of changes, which is exactly the staleness problem `index-storage-and-scanning`'s incremental-update and reconciliation design already exists to avoid for the search index; storage sizes should get the same guarantee, not a weaker one.

**Fallback when a size isn't known yet** (mid-initial-scan, or a subtree newer than the mapped index snapshot): storage-analysis requests the aggregate, receives an immediate "pending" response, and computes it asynchronously (background aggregation over just that subtree, or waiting for the index to catch up) without blocking the UI thread. The affected row/rectangle shows an explicit "Calculating…" affordance — never a blank, a zero, or a stale number presented as final — and updates in place once the real value resolves, with no manual refresh required.

### D2: Squarified treemap layout

The treemap uses the squarified treemap algorithm (Bruls, Huizing, van Wijk) rather than simple slice-and-dice layout.

**Why:** slice-and-dice (splitting a container into all-horizontal or all-vertical strips) produces pathologically thin slivers as the number of siblings grows — bad for visually comparing area (the whole point of a treemap) and bad for mouse precision on small items. Squarified layout keeps rectangle aspect ratios close to 1:1 across a wide range of sibling counts and size distributions, which is precisely the failure mode this feature must avoid (a directory with hundreds of small files is a common case, not an edge case). This is the standard technique used by comparable tools (WinDirStat-style disk usage visualizers).

**Alternatives considered:**
1. *Slice-and-dice.* Rejected — simpler (`O(n log n)` sort-and-divide) but produces poor aspect ratios exactly when there are many small children, which is common for real directories.
2. *Strip/pivot treemap variants.* Considered as a middle ground, but squarified has the strongest aspect-ratio guarantee and the most established prior art for this exact problem (many small files sharing a container), so the extra implementation complexity over slice-and-dice is worth paying once.

Layout is computed directly from already-aggregated size data (no filesystem access at layout time) and is cached as a layout tree, recomputed only when the underlying dataset (selected scope, depth, active filter) or the render viewport size changes — not on every frame.

### D3: Hit-testing reuses the cached layout tree via recursive bounds descent

Hover and click resolution walk the same rectangle tree already produced for rendering (D2), recursively descending into the child rectangle that contains the cursor point until reaching a leaf or a rectangle below the minimum-visible-size threshold.

**Why:** the number of simultaneously rendered rectangles is bounded by screen area (rectangles below a minimum pixel size are grouped into an "other small items" catch-all rather than individually rendered), so recursive descent is effectively `O(depth)`, not `O(total items in the volume)`. Reusing the render layout tree also guarantees hover/click always agree with what's visually on screen, since they read the same structure.

**Alternatives considered:**
1. *A general-purpose spatial index (R-tree/quadtree) over all rectangles.* Rejected as unnecessary complexity — the visible rectangle count per frame is already small and bounded by the render-time size cutoff, so a full spatial index solves a scale problem this feature doesn't have.
2. *Recomputing hit regions from raw data on every hover/click.* Rejected — wasteful; hit-testing is invalidated on exactly the same triggers as layout (D2: data change or viewport resize), not per pointer move.

### D4: Extension-to-category mapping is configurable data, not compiled constants

The mapping is a category → ordered list of extension patterns structure (category id, display name, extension patterns, e.g. `*.mp4`), shipped with sensible defaults (video, image, document, archive, executable, development files, VM images, games, etc.) but stored and edited as user-editable configuration, not hardcoded switch/lookup logic.

**Why:** file-type taxonomies change constantly (new archive formats, new dev-tool file extensions, new game-engine asset formats) and users legitimately disagree about grouping (e.g., wanting `.blend` treated as its own "3D assets" category). Hardcoding would force a release for every such change. Making the mapping data — with `settings-and-appearance` building an editing UI against it later — means it can be extended without touching storage-analysis's code at all. This is explicitly noted as the dependency `settings-and-appearance` will build on.

**Alternatives considered:**
1. *Hardcoded mapping compiled into the binary.* Rejected — blocks user customization and ties every new extension to a release cycle.
2. *Rely solely on Windows' registered file-type associations (`PerceivedType` and similar registry data).* Rejected as the primary mechanism — coverage is inconsistent for exactly the audience-relevant categories called out (development files, VM images, game assets are frequently unregistered or registered inconsistently across machines), and it would make categorization depend on what happens to be installed locally rather than being a stable, portable FastFiles-owned default. It remains a candidate low-priority supplemental hint, not the primary source.

Matching is case-insensitive; when more than one configured pattern could match, the most specific pattern wins; any extension matching nothing (or a file with no extension) falls into an explicit "Other/Uncategorized" bucket that participates in totals rather than being silently dropped.

### D5: Track both logical size and allocated (on-disk) size

Both values are tracked and exposed, not logical size alone.

**Why this is affordable, not just desirable:** per `establish-architecture-foundation`'s D4, the privileged parser already reads only `$STANDARD_INFORMATION` and `$FILE_NAME` — never `$DATA` — specifically to avoid forwarding file content. The NTFS `$FILE_NAME` attribute itself carries cached `AllocatedSize` and `RealSize` (logical size) fields for exactly this purpose (this is how MFT-based tools resolve sizes without opening files at all), so both numbers are already available from data the parser is designed to touch — there is no incremental privileged-access cost to carrying both.

**Why it matters for this product specifically:** WizTree — a named inspiration for this pillar — treats size-on-disk as a first-class, differentiating number precisely because compression and sparse files can make logical and allocated size diverge enormously. One of the shipped default categories is VM images, which are exactly the case where a sparse `.vhdx` can report a large logical size against a much smaller real disk footprint — the case users most want an accurate answer for. Shipping logical-size-only would be a visible regression against the tool this pillar is modeled on, for close to zero added engineering cost.

**Decision:** the treemap's rectangle area is driven by logical size by default (matching the number most other views — Explorer, search results — show as "the size" of a file), while allocated size is surfaced alongside logical size in hover/detail metadata, called out specifically when the two diverge meaningfully, so compressed/sparse items are explainable rather than mysterious. Whether to let users toggle which metric drives treemap area is left open (see Open Questions).

**Alternative considered:** logical-size-only for MVP, deferring allocated size to a follow-up. Rejected — the marginal engineering cost is close to zero given D5's data is already flowing through the parser design, while the trust cost of a WizTree-inspired tool silently misreporting real disk usage on compressed/sparse volumes is exactly the kind of gap this product cannot afford in its first storage-analysis release.

**Note on volume-level totals:** `storage-overview`'s total/used/free/percentage figures are sourced directly from the OS (`GetDiskFreeSpaceEx`), not derived from summing indexed item sizes — the two can legitimately disagree (system/hidden files, alternate data streams, filesystem metadata overhead, excluded paths), and that disagreement must be explained, not hidden by quietly substituting one number for the other.

### D6: Storage-analysis is structurally non-destructive

No requirement in `storage-overview`, `storage-drilldown-and-treemap`, or `file-type-categorization` includes any create/delete/move/rename/write behavior. Every interactive affordance is one of: (a) a read (list, sort, filter, hover), or (b) a navigation hand-off into `column-view-browsing`/`navigation-and-workspace`, or (c) an explicit hand-off into `file-operations-core`'s own confirmation-and-execution flow for anything destructive (e.g., a "Delete" context action on a treemap item opens the same confirmation/Recycle-Bin flow file-operations-core already owns, rather than a shortcut that bypasses it).

**Why:** duplicating even a "quick delete" shortcut inside storage-analysis would risk diverging from file-operations-core's Recycle-Bin-by-default and conflict-handling guarantees, and directly contradicts this change's own proposal, which states storage analysis never performs destructive actions on its own.

**Alternative considered:** a "quick delete from treemap" shortcut for speed, bypassing the standard confirmation flow. Rejected outright — this is the one boundary the proposal treats as non-negotiable.

## Risks / Trade-offs

- **[Risk]** Summed indexed sizes (logical or allocated) will not exactly equal the OS-reported used space for a volume (system files, ADS, filesystem metadata, excluded paths). → **Mitigation:** `storage-overview` always sources total/used/free from the OS as ground truth (D5); drill-down/treemap totals are explicitly labeled as "of indexed items," with the gap explainable rather than presented as a discrepancy or bug.
- **[Risk]** Incrementally maintained aggregates in `filesystem-index-store` can drift from ground truth over time (missed events, crash mid-update). → **Mitigation:** relies on `index-storage-and-scanning`'s periodic reconciliation sweeps to self-correct; storage-analysis does not attempt independent verification of aggregate correctness, and should not present a drifted number with more confidence than it deserves — pairing with per-volume index-health status (`settings-and-appearance`'s `index-health-and-diagnostics`) is a natural follow-up, tracked as coordination rather than owned here.
- **[Risk]** Degraded mode means only browsed/pinned directories have any index coverage — a truthful whole-volume treemap/drill-down cannot be produced. → **Mitigation:** this is the architecture's accepted, permanent trade-off (D5 of `establish-architecture-foundation`); `storage-overview` still shows correct OS-level capacity even in degraded mode, and drill-down/treemap explicitly label partial (browsed/pinned-only) coverage rather than implying whole-volume completeness.
- **[Risk]** Re-laying-out the treemap too often (e.g., on every hover) would be wasteful and could introduce visible jank. → **Mitigation:** explicit, narrow invalidation triggers only — data change or viewport resize (D2/D3) — never per-frame or per-pointer-move.
- **[Risk]** Extension-based categorization is inherently heuristic (extension doesn't guarantee content, extensionless or renamed files exist). → **Mitigation:** an explicit "Other/Uncategorized" bucket keeps totals honest, and a user-editable mapping (D4) lets misclassification be corrected without a code change; content-sniffing is explicitly out of scope for this MVP.
- **[Risk]** Showing a treemap area driven by logical size while allocated size differs significantly (compressed volumes, VM image sparse files) could mislead a user about real reclaimable space if not called out. → **Mitigation:** D5's hover/detail panel surfaces both numbers and calls out meaningful divergence explicitly.
- **[Risk]** A "Calculating…" affordance that never resolves (e.g., background aggregation starved by higher-priority index work) would be worse than an honest error. → **Mitigation:** background aggregation for pending sizes needs a defined (even if low) scheduling priority relative to interactive index work, and the UI must distinguish "still calculating" from "became unavailable" (e.g., path deleted mid-calculation) rather than leaving a spinner indefinitely — tracked as an implementation detail in tasks.md.

## Migration Plan

Greenfield addition — no existing users or persisted storage-analysis data to migrate. This change introduces no durable schema of its own beyond the user-editable category mapping (a small, versionable configuration artifact); the size/hierarchy data it reads is owned and migrated, if ever, by `filesystem-index-store`. No rollback-of-data concerns apply.

## Open Questions

- Exact storage location/format for the extension-to-category mapping (embedded config file vs. a small table inside `FastFilesEngine`'s existing SQLite store) is left to implementation, coordinated with `settings-and-appearance`'s editing UI.
- Whether treemap area should be user-toggleable between logical and allocated size, or fixed to logical-by-default as this design assumes, is not fully settled.
- The precise API/contract `filesystem-index-store` exposes for incremental aggregate maintenance (delta-applied on the ancestor chain vs. lazy dirty-marking plus async recompute) is `index-storage-and-scanning`'s decision to finalize; this design assumes an incremental, non-blocking contract exists and specifies only storage-analysis's consumption and fallback behavior.
- Whether largest-files/largest-folders views are served by a maintained top-N structure in `filesystem-index-store`, versus computed on demand from the in-memory projection at query time (still fast — an in-memory scan, not a disk walk) — leaning toward on-demand, but flagged as a coordination point rather than decided here.
- The concrete minimum-rectangle-size / small-items-catch-all threshold for the treemap is left to implementation/UX tuning rather than fixed in this design.
