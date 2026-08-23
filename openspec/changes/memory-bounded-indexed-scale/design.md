# Design: Memory-Bounded Indexed Scale

## Context

`Projection` is a bounded LRU working set over the durable `Store`
(`kDefaultMaximumMemoryBytes` = 500 MiB; subtree eviction; `evictedVolumes_`
marks any volume that lost at least one subtree). The Store commits every
ingestion change **before** the Projection applies it (D4), so the durable
database is always a superset of the resident projection. Indexed search,
however, wants "all rows visible" and therefore called
`ReloadAllEvictedVolumes()` before enumerating — reconstructing the entire
durable index in RAM per query once eviction has occurred.

Measured defect (reproduced in `ffengine_index_pipeline_tests` with a
deliberately tiny budget): after forcing eviction, each search reloads the full
volume under the pipeline mutex, the next ingestion batch re-evicts, and the
following search reloads again — unbounded latency/memory churn. If a reload
fails (volume metadata missing), the boolean return is discarded and the search
completes **without the evicted rows**: silent partial results presented as
complete.

## Goals / Non-Goals

**Goals**
- Complete, order-identical indexed results regardless of residency.
- Persistent projection memory never grows because of searching.
- No thrash: repeated cold searches do not reload/evict cycles.
- Bounded transient allocations, documented separately from persistent state.
- Snapshot publication bounded by the browse working set, not tree size.

**Non-Goals**
- Raising the 500 MiB default, disabling eviction, or adding caches without
  bounds.
- Any new wire-protocol message or snapshot format version.
- Changes to authentication, signing pins, or the privileged path.

## Decisions

### D1 — Per-volume hot/cold partition (search)

`SearchIndexed` classifies each searchable volume: hot if
`!projection_.NeedsReload(id)`, cold otherwise. Enumeration visits:

1. Hot volumes: projection rows only (`ForEachEntryValueUntil`, existing
   behavior), skipping cold-volume residue rows.
2. Cold volumes: `store_.ForEachEntry(volumeRowId, …)` streaming rows in one
   pass, converting `EntryRecord → CandidateView` into caller-owned scratch
   (one reused name buffer per row).

Correctness rests on the store-superset invariant: for a marked-evicted volume
the projection holds only residue, so serving that volume **exclusively** from
the Store yields exactly its durable rows with zero double counting; hot
volumes are unchanged. The two sources never mix within one volume, so no
per-key dedup pass is needed.

Trigram rule: postings index only resident names, so the accelerator is used
iff **every** searched volume is hot (today's exact behavior); any cold volume
forces the canonical scan over both sources. The accelerator therefore can
never cause a false negative relative to canonical semantics.

Failure honesty: if a cold stream aborts early (SQLite error) without
cancellation, the response is `Unavailable` — never partial-but-complete-looking.

Cancellation checks run between source segments and inside the stream visitor;
a cancelled cold search stops streaming immediately.

### D2 — Cold folder reconstruction

Matched candidates carry parent folders via lazy materialization. Hot rows use
the existing `Projection::ReconstructPath`. Cold rows need a durable walk:
new `Store::FindEntry(volumeRowId, FileId)` (prepared point SELECT) powers
`MaterializeCandidateFolderHybrid`, which tries the projection first and falls
back to walking `FindEntry` parent chains (depth-bounded, cycle-safe). Only
path-dependent survivors pay this cost (≤ maxResults ≤ 4096 walks).

The owning (non-view-compatible) fallback path gains the same hybrid
materializer while holding the pipeline mutex.

### D3 — Cold aggregates without reload

`IndexPipeline::GetFolderAggregate` currently calls
`ReloadEvictedVolume` first — hydrating an entire volume to sum one subtree.
Instead: hot volume → projection walk (unchanged); evicted volume →
`Store::GetFolderAggregate` recursive CTE (already exists for exactly this
fallback role), mapped onto `Projection::FolderAggregate` with
`complete = (knownCount == totalCount)`. Saturation semantics stay with the
projection path; the store path reports SQLite SUM semantics as documented
there.

`ExportDirectorySnapshot` retains single-volume hydration deliberately: it is
invoked from bulk scan/rebuild completion where the export itself needs
coherent whole-subtree structure, and D4 bounds what it produces.

### D4 — Bounded snapshot publication (working-set model)

Two bounds replace "serialize the entire tree":

1. **Bounded bulk export**: new
   `IndexPipeline::ExportDirectorySnapshot(id, prefix, const ExportOptions&)`
   with `{ maxDirectories }` (default unlimited for compatibility). Startup
   rebuild (Main.cpp) and scan-complete publication (VolumeSessionManager)
   pass `kInitialIndexedExportDirectories` (4096), exporting directories in the
   map's deterministic path-sorted order — i.e., top-of-tree-first breadth.
   Deeper directories remain durable and re-materialize on demand.
2. **UiServer retained working set**: `directories_` caps *indexed*
   (`volumeRowId != 0`) entries at `kMaxRetainedIndexedDirectories` (16384,
   test-configurable). Each observation (indexed merge, degraded request,
   watcher refresh) stamps a monotonic epoch; when a merge exceeds the cap, the
   lowest-epoch indexed entries are dropped. Degraded entries (actively browsed,
   `volumeRowId == 0`) are always retained. Stale-key cleanup on complete
   per-volume exports is preserved exactly (deleted/renamed paths cannot
   linger), and the observation-precedence rules from
   `deterministic-missing-path-state` are untouched.

Hydration of a cold indexed directory = the existing `RequestDirectory`
degraded enumeration + watch (online volumes self-heal with one round trip);
offline volumes stay honestly absent (`RemoveVolumeDirectories`). Publish
remains immutable-generation, fail-closed above 256 MiB, version-checked.

### D5 — Memory diagnostics

Projection tracks monotonic counters: subtree evictions, cold reloads,
bytes-before/after last eviction; exposed as `Projection::MemoryDiagnostics`.
`SearchTimings` gains hot/cold row counts, cold-volume count, and store-stream
time. Tests assert the policy: forced small budget + repeated mixed hot/cold
queries keep `EstimatedMemoryBytes() <= maximum` persistently with a bounded
eviction count (no unbounded reload→evict cycling — structurally impossible
now that search never reloads).

## Risks / Trade-offs

- Cold searches are slower than RAM (SQLite stream per query). Accepted:
  bounded, cancellable, and strictly better than the previous reload-everything
  path; repeated cold searches now cost O(matched work) not O(reload).
- Capped retention means an indexed directory outside the working set may need
  one degraded round trip on first navigation. Accepted; identical to how
  non-indexed paths already behave, and bounded by design.
- Store-side aggregates lose the projection's saturating arithmetic for
  evicted volumes (SQLite float-promotion overflow semantics). Documented;
  completeness pair still explicit.

## Migration Plan

Purely internal engine/indexstore changes; no persisted-format or protocol
change. Old databases behave identically. Rollback = revert the three engine
commits.

## Open Questions

None — measurement wave (bench_large_index_scalability refresh) validates the
chosen bounds rather than selecting between alternatives.
