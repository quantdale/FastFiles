# Memory-Bounded Indexed Scale — Tasks

## 1. Reconcile and specify

- [x] 1.1 Establish branch `phase8/indexed-scale-production-readiness-20260824`; record PR #6 reconciliation disposition (superseded deltas; palette/contrast work already ported on `overnight/release-closure-20260823`). — complete: current branch and PR disposition are recorded in `docs/pr6-stabilization-reconciliation.md`; no stale PR implementation was merged.
- [x] 1.2 Author this change's proposal/design/spec deltas before architecture-changing edits. — complete: proposal, design, and both capability deltas were present before implementation commits.

## 2. Cold-data access path

- [x] 2.1 Add `Store::FindEntry(volumeRowId, FileId)` point lookup with full field round-trip (allocated-size unknown flag included). — complete: `Store::FindEntry` and `TestFindEntryPointLookup` round-trip all persisted fields, including allocated-size unknown state.
- [x] 2.2 Remove both `ReloadAllEvictedVolumes` calls from `SearchIndexed`; implement per-volume hot/cold partition with store streaming for cold volumes. — complete: `SearchIndexed` partitions hot/cold volumes and cold rows stream through `ForEachEntryUntil`; no search path reloads evicted volumes.
- [x] 2.3 Trigram accelerator only when every searched volume is hot; canonical mixed enumeration otherwise. — complete: forced-eviction equivalence tests assert the accelerator is disabled for cold searchable sets and all matches remain discoverable.
- [x] 2.4 Hybrid candidate folder reconstruction: projection walk first, durable `FindEntry` parent-chain fallback, depth-bounded and cycle-safe. — complete: cold search and post-eviction move tests reconstruct durable parent chains.
- [x] 2.5 Fail closed (`Unavailable`) on a failed cold store stream without cancellation. — complete: cold-stream failure paths return `Unavailable` and discard partial results.
- [x] 2.6 Cancellation checks between source segments and inside cold stream visitor. — complete: cold cancellation tests observe `Cancelled` and stop before exhausting the durable stream.

## 3. Memory policy hardening

- [x] 3.1 Projection `MemoryDiagnostics` (configured max, estimated bytes, last-eviction bytes before/after, subtree-eviction count, cold-reload count). — complete: diagnostics are exposed and covered by Projection tests.
- [x] 3.2 SearchTimings hot/cold metrics (hot rows, cold rows, cold volumes, store-stream ms). — complete: `IndexPipeline::SearchTimings` records hot/cold rows, cold volumes, and stream duration; forced-eviction tests assert attribution.
- [x] 3.3 Regression tests: forced small budget keeps estimated bytes within budget across repeated queries; eviction counts bounded; no reload→evict thrash from searching. — complete: repeated cold-search regression performs 25 queries with zero additional evictions or whole-volume reloads.

## 4. Cold browse/aggregates

- [x] 4.1 Evicted-volume folder aggregate served by the store recursive CTE mapped onto the projection aggregate shape (completeness pair preserved); no whole-volume reload. — complete: cold aggregate tests match the durable recursive CTE and assert no reload.
- [x] 4.2 Tests: evicted-subtree search hit, no-match, path-scoped, Unicode, unavailable volume, mutations after eviction (insert/rename/move/delete), restart-from-SQLite equivalence. — complete: IndexPipeline and seeded acceptance suites cover these cases and pass.

## 5. Bounded snapshot publication

- [x] 5.1 `ExportDirectorySnapshot` bounded-export options; startup rebuild + scan-complete use a top-of-tree bound constant. — complete: bounded export is wired at startup and scan completion with `kInitialIndexedExportDirectories`.
- [x] 5.2 UiServer retained indexed-directory working-set cap with observation-epoch eviction; degraded entries always retained; stale-key cleanup and precedence rules unchanged. — complete: `UiServer` enforces recency-ordered indexed retention while exempting degraded entries and preserving cleanup.
- [x] 5.3 Tests: cap enforcement, recency retention, republish generation change, cold re-materialization via request path, deletion cleanup, unavailable volume, multi-client generation broadcast. — complete: lifecycle and seeded acceptance tests pass these working-set and generation scenarios.

## 6. Seeded indexed-mode acceptance

- [x] 6.1 New deterministic integration target `ffengine_seeded_indexed_mode_tests`: synthetic MFT/USN-equivalent population of the real Store/IndexPipeline. — complete: target populates the production Store/IndexPipeline seams without privileged bypasses.
- [x] 6.2 Cover first population, restart, navigation data export, indexed + trigram + canonical-fallback searches, aggregates, allocated size, rename/move/delete, reconciliation removal, unavailable volume, eviction/cold search, cancellation, snapshot publish→parse, deterministic final state. — complete: seeded acceptance reports every listed stage and passes.
- [x] 6.3 Register with CTest (default suite). — complete: `ffengine_seeded_indexed_mode_tests` is registered in `tests/engine/CMakeLists.txt` and ran successfully.

## 7. Scale evidence

- [x] 7.1 Refresh `bench_large_index_scalability` measurements on current architecture at executed scales; record results as versioned evidence under this change's `evidence/`.
- [x] 7.2 Confirm no active documentation claims the obsolete slot-era snapshot design.

## 8. Validation

- [x] 8.1 Focused: indexstore, index-pipeline, ui-server, seeded-suite tests pass.
- [x] 8.2 Full Debug build + CTest; release-test build + CTest; analyze build clean.
- [x] 8.3 ASAN protocol+IPC fuzz targets pass.
- [x] 8.4 Strict OpenSpec validation passes; docs drift guard passes.
