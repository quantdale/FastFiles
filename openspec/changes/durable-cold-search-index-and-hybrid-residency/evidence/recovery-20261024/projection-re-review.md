## Review

**Reviewed SHA:** `08be96fa2717cbf5daf9123345a23840c43fb4df`, independently confirmed from the branch ref. The reviewed projection files remained unchanged during inspection.

**Scope:** Re-review of Lane A’s P1/P2 repairs—not final campaign approval. Inspected saved `repair.diff`, actual `Projection.cpp`, `FlatHashMap.h`, projection tests, relevant pipeline epoch handling, existing concurrency tests, tasks/design and supplied red/green logs. No writes, builds or tests performed.

### Fixed: A P1 — Quadratic whole-volume sibling unlinking

`Projection::EvictVolume` now snapshots matching keys, invalidates the accelerator, erases identities directly and compacts once (`src/indexstore/src/Projection.cpp:419–437`).

This removes the problematic call chain through `Remove` → `FlatChildrenMap::remove`. Compaction reconstructs child links exclusively from surviving identities (`Projection.cpp:455–467`), so skipping doomed rows’ child unlinking is correct.

The new 20k-sibling regression checks removal-comparison work and preservation of another volume’s entries/child links (`tests/indexstore/test_projection.cpp:1422–1438`). The counter observes the actual linear-search loop and is test-only (`src/indexstore/include/ffindexstore/FlatHashMap.h:278–282,411–413`; `tests/indexstore/CMakeLists.txt:11–14`).

**Disposition: closed.** This proves removal of the identified quadratic unlink mechanism, not universal linear complexity for every compaction topology.

### Fixed: A P2 — Retained free-list allocation defeats convergence

`CompactStorage` now swaps `freeList_` with an empty vector after replacing the row arrays (`Projection.cpp:467–476`). All surviving rows have already been densely repacked, so no free slots remain to preserve. Releasing that allocation is correct for every compaction caller.

The enlarged 20k-row tiny-victim fixture exercises automatic enforcement at a 64 KiB budget (`test_projection.cpp:1404–1420`). Its budget assertion now addresses the previously failing oversized free-list case.

**Disposition: closed.**

### Correct: Blast radius is appropriately bounded

- Surviving rows retain their metadata, names and access timestamps through compaction (`Projection.cpp:455–463`).
- Whole-volume eviction still cold-marks the target and increments the eviction counter once (`:435–437`).
- Accelerator invalidation remains consistent with the existing trailing compaction.
- Ordinary `Remove`, `RemoveBatch` and `RemoveVolume` retain their existing semantics (`:838–917`).
- Releasing free-list capacity does not change subsequent insertion correctness: a densely packed projection appends new rows (`:750–757`).
- Automatic ingestion enforcement still advances structural residency epochs (`src/engine/src/IndexPipeline.cpp:819–820`), including other volumes whose row slots were repacked (`:426–442`).

No issues found.

### Evidence and remaining coverage

The preserved `projection-red.log:1–2` records both expected failing assertions. `green-focused.log:1–20` records seven passing focused suites, including projection, pipeline and concurrency tests. I inspected these records but did not independently execute or bind their binaries to the reviewed SHA.

The following remain necessary to close the **original scenario matrix**, rather than to establish these two local fixes:

1. **Automatic fallback during an active search:** pause after capture, trigger ingestion-driven fallback, then require restart and exact results. Existing concurrency coverage uses explicit unload (`tests/engine/test_index_pipeline_concurrency.cpp:624–669`).
2. **Automatic multi-volume pressure:** retain a small hot volume while a large volume falls cold; verify surviving paths, aggregates, LRU timestamps and exact scoped/global results. The new test verifies basic survivor identity and child links only.
3. **Ingestion/search/export alternation:** exercise repeated fallback followed by export/reload attempts and subsequent mutations; verify cold completeness, bounded memory and reader cleanup.

Useful additional assertions in the budget regression: empty resident row count, zero retained free-list slots and a second enforcement call that remains within budget.

### Merge verdict: OK with notes

**The repaired projection source is acceptable; both original A findings are closed.** The remaining integration scenarios and final-source performance measurements are not certified by this re-review. This is not approval of the complete repair commit or PR15 campaign.