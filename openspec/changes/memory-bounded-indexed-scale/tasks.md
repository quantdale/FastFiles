# Memory-Bounded Indexed Scale — Tasks

## 1. Reconcile and specify

- [ ] 1.1 Establish branch `phase8/indexed-scale-production-readiness-20260824`; record PR #6 reconciliation disposition (superseded deltas; palette/contrast work already ported on `overnight/release-closure-20260823`).
- [ ] 1.2 Author this change's proposal/design/spec deltas before architecture-changing edits.

## 2. Cold-data access path

- [ ] 2.1 Add `Store::FindEntry(volumeRowId, FileId)` point lookup with full field round-trip (allocated-size unknown flag included).
- [ ] 2.2 Remove both `ReloadAllEvictedVolumes` calls from `SearchIndexed`; implement per-volume hot/cold partition with store streaming for cold volumes.
- [ ] 2.3 Trigram accelerator only when every searched volume is hot; canonical mixed enumeration otherwise.
- [ ] 2.4 Hybrid candidate folder reconstruction: projection walk first, durable `FindEntry` parent-chain fallback, depth-bounded and cycle-safe.
- [ ] 2.5 Fail closed (`Unavailable`) on a failed cold store stream without cancellation.
- [ ] 2.6 Cancellation checks between source segments and inside cold stream visitor.

## 3. Memory policy hardening

- [ ] 3.1 Projection `MemoryDiagnostics` (configured max, estimated bytes, last-eviction bytes before/after, subtree-eviction count, cold-reload count).
- [ ] 3.2 SearchTimings hot/cold metrics (hot rows, cold rows, cold volumes, store-stream ms).
- [ ] 3.3 Regression tests: forced small budget keeps estimated bytes within budget across repeated queries; eviction counts bounded; no reload→evict thrash from searching.

## 4. Cold browse/aggregates

- [ ] 4.1 Evicted-volume folder aggregate served by the store recursive CTE mapped onto the projection aggregate shape (completeness pair preserved); no whole-volume reload.
- [ ] 4.2 Tests: evicted-subtree search hit, no-match, path-scoped, Unicode, unavailable volume, mutations after eviction (insert/rename/move/delete), restart-from-SQLite equivalence.

## 5. Bounded snapshot publication

- [ ] 5.1 `ExportDirectorySnapshot` bounded-export options; startup rebuild + scan-complete use a top-of-tree bound constant.
- [ ] 5.2 UiServer retained indexed-directory working-set cap with observation-epoch eviction; degraded entries always retained; stale-key cleanup and precedence rules unchanged.
- [ ] 5.3 Tests: cap enforcement, recency retention, republish generation change, cold re-materialization via request path, deletion cleanup, unavailable volume, multi-client generation broadcast.

## 6. Seeded indexed-mode acceptance

- [ ] 6.1 New deterministic integration target `ffengine_seeded_indexed_mode_tests`: synthetic MFT/USN-equivalent population of the real Store/IndexPipeline.
- [ ] 6.2 Cover first population, restart, navigation data export, indexed + trigram + canonical-fallback searches, aggregates, allocated size, rename/move/delete, reconciliation removal, unavailable volume, eviction/cold search, cancellation, snapshot publish→parse, deterministic final state.
- [ ] 6.3 Register with CTest (default suite).

## 7. Scale evidence

- [x] 7.1 Refresh `bench_large_index_scalability` measurements on current architecture at executed scales; record results as versioned evidence under this change's `evidence/`.
- [x] 7.2 Confirm no active documentation claims the obsolete slot-era snapshot design.

## 8. Validation

- [x] 8.1 Focused: indexstore, index-pipeline, ui-server, seeded-suite tests pass.
- [x] 8.2 Full Debug build + CTest; release-test build + CTest; analyze build clean.
- [x] 8.3 ASAN protocol+IPC fuzz targets pass.
- [x] 8.4 Strict OpenSpec validation passes; docs drift guard passes.
