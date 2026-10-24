## Review

**Reviewed SHA:** `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`, independently confirmed from the worktree’s branch ref.

**Scope:** Lane A: projection eviction/convergence, residency, reloads, cursors, query epochs, publication and concurrent search.

I inspected actual source and the supplied Git-generated diffs:
- `build/campaign/recovery-20261024/eviction.diff`: `5d7e98a^..5d7e98a`.
- `build/campaign/recovery-20261024/pr15-executable.diff`: `d3884ee..8c4c848`.
- `evidence-range.txt`: production baseline `5d7e98a` to reviewed HEAD.

No tests were executed, files edited or restricted evidence directories accessed. Previous review reports are not treated as certification.

### Correct

- **Durable-first ingestion remains intact.** `ApplyMftBatchChanges` and `ApplyUsnBatchChanges` commit SQLite before changing the projection (`src/engine/src/IndexPipeline.cpp:745–754,885–900`).
- **Automatic enforcement changes invalidate captured projection layouts.** Both ingestion paths call `AdvanceResidencyEpochsForStructuralChangeLocked` when enforcement evicts (`IndexPipeline.cpp:819–820,967–968`). That helper advances every tracked volume’s data epoch and the dependency-set epoch (`:426–442`).
- **Cold search does not reload the volume.** Search captures residency and a separate read session under the mutex, skips cold volumes during projection traversal and streams their durable rows (`IndexPipeline.cpp:1539–1564,1940–1972`). `StoreReadSession::Open` pins its SQLite snapshot before releasing the pipeline mutex (`src/indexstore/src/Store.cpp:1709–1727`).
- **Normal search validates epochs; sustained churn has a coherent durable fallback.** See `IndexPipeline.cpp:1586–1602,1983–2002,2040–2149`.
- **Snapshot serialization remains commit-before-handoff.** The generation marker is written after the payload; the publisher replaces the current mapping only after successful creation (`src/engine/src/SnapshotPublisher.cpp:174–205,249–278`).
- Existing tests cover explicit-unload search equivalence, cold mutations, repeated cold searches, cursor growth and partial reconciliation failure. These are useful coverage, but do not establish the new fallback’s topology-dependent cost.

### Finding: P1 — Whole-volume fallback introduces quadratic sibling unlinking into automatic enforcement

**Location:** `src/indexstore/src/Projection.cpp:509–513`, calling `EvictVolume` at `:419–443`.

**Evidence:**
1. The new fallback automatically evicts an entire volume after the bounded fine-grained passes.
2. `EvictVolume` calls `Remove` individually for every collected key (`:437–438`).
3. `Remove` unlinks each row from its parent’s children (`:866–867`).
4. `FlatChildrenMap::remove` linearly searches that parent’s remaining child slice (`src/indexstore/include/ffindexstore/FlatHashMap.h:399–416`).

Consequently, a directory containing **N sibling files** can require **Θ(N²)** child-index comparisons during whole-volume eviction. Hash-table iteration does not supply the reverse-child removal order needed to avoid this. For an order-preserving, noncolliding layout, the first approximately N/2 removals alone scan positions `0,1,…`, giving at least approximately N²/8 comparisons.

This bulk-removal implementation predates the change, but **the diff newly invokes it automatically during memory enforcement**. Previously this path performed at most eight fine-grained passes. Ingestion holds the pipeline mutex throughout (`IndexPipeline.cpp:739–740,819`), so large-fanout eviction can also block search captures, materialization and snapshot exports.

**Minimal reproduction:** Extend `TestTinyVictimsCannotSustainAnOverBudgetProjection` (`tests/indexstore/test_projection.cpp:1404–1420`) to increasing sibling populations, such as 20k/40k/80k, while keeping its empty-directory victims and budget. Measure enforcement separately or count child-index comparisons. A production-scale equivalent is a large flat root crossing the watermark.

**Verification status:** Algorithmic defect proven from the call chain; no runtime timing is claimed.

**Smallest fix:** Make whole-volume eviction a genuine bulk erase: omit child-list unlinking for doomed rows, then rebuild surviving child links in the already-required `CompactStorage`. Preserve ordinary `Remove` semantics elsewhere.

### Finding: P2 — Fallback can empty the projection while permanently remaining over budget

**Location:** New fallback at `src/indexstore/src/Projection.cpp:509–518`; retained allocation at `:475`.

**Evidence:**
- Each removed row is appended to `freeList_` (`:871`).
- `CompactStorage` calls only `freeList_.clear()` (`:475`), retaining its allocation.
- `EstimatedMemoryBytes` includes `freeList_.capacity() * sizeof(uint32_t)` (`:175–176`).
- After whole-volume eviction empties the projection, the fallback has no remaining victim and exits (`:510–511`).

Thus, the newly added whole-volume convergence path can create a zero-row projection whose retained free-list allocation alone exceeds the configured budget. Repeated enforcement cannot reclaim it.

**Minimal reproduction:** Use the new tiny-victim test unchanged except increase its file population to 20,000. With the existing 64 KiB budget, removing those rows requires at least approximately 80 KiB of free-list capacity. After enforcement, check:
- `EntryCount() == 0`;
- `EstimatedMemoryBytes() > 64 * 1024`;
- another enforcement call does not restore the budget.

The retained-capacity mechanism is older code; the new fallback makes this all-rows-removed/no-victim state reachable through `EnforceMemoryLimitNow`.

**Verification status:** Allocation lower bound proven from source; proposed runtime reproduction was not executed. This is primarily a small-budget/oversized-batch boundary issue, not evidence that the default-budget fixture fails.

**Smallest fix:** Release the now-unused free-list allocation during compaction, rather than merely clearing its size.

### Concrete coverage gaps

1. **Large fanout:** The new 10k-sibling test checks final memory only, not unlink work or scaling (`test_projection.cpp:1404–1420`).
2. **Automatic fallback during concurrent search:** The concurrency regression uses explicit `UnloadVolumeFromProjection`, not an ingestion batch exhausting eight passes (`tests/engine/test_index_pipeline_concurrency.cpp:624–669`).
3. **Mixed volumes under automatic pressure:** Existing hot/cold equivalence explicitly unloads the cold volume (`tests/engine/test_index_pipeline.cpp:1445–1449`). Add small-hot/large-cold pressure tests asserting residency, exact results and scoped-search behavior.
4. **Ingestion/search/export alternation:** Repeated cold searches are covered (`test_index_pipeline.cpp:1591–1647`), but not repeated ingestion-triggered whole-volume eviction interleaved with export/reload attempts.
5. **Publication mid-transition:** Add a barrier-controlled search spanning automatic fallback and an export/reload attempt, checking epoch restart and complete durable results.
6. **Oversized allocation convergence:** Assert both the watermark and retained-capacity behavior after the projection becomes empty—not just `NeedsReload`.

### Exact inspected seams

- `src/indexstore/src/Projection.cpp`: `EnforceMemoryLimit`, `EnforceMemoryLimitOnceTo`, `EnforceMemoryLimitTo`, `LeastRecentlyUsedEntry`, `EvictSubtree`, `EvictVolume`, `CompactStorage`, `Remove`, `RemoveBatch`, `RemoveVolume`, `Upsert`, `UpsertPartial`, `ReloadEvictedVolume`, `RebuildVolumeFromStore`, `ProjectionEntryCursor::{Stale,Next}`.
- `src/indexstore/include/ffindexstore/{Projection.h,FlatHashMap.h}`: enforcement contracts, cursor state, map iteration and child removal.
- `src/engine/src/IndexPipeline.cpp`: ingestion, residency/query epochs, reconciliation, export, unload, budget changes and `SearchIndexedReadPlane`.
- `src/indexstore/src/Store.cpp`: read-session snapshot establishment.
- Publication callers in `Main.cpp`, `VolumeSessionManager.cpp`, `UiServer.cpp`; generation commit in `SnapshotPublisher.cpp`.
- Tests: `test_projection.cpp`, `test_index_pipeline.cpp`, `test_index_pipeline_concurrency.cpp`, `bench_identity_ordering_oracle.cpp`; applicable CMake registrations.
- Root `AGENTS.md`, workstate, and relevant durable-cold-search tasks/design plus predecessor design.

### Merge verdict: BLOCK

Fix the newly automatic quadratic whole-volume removal before merging. The retained-capacity boundary issue should also receive a focused regression. This verdict is scoped to Lane A; it is not whole-PR certification.