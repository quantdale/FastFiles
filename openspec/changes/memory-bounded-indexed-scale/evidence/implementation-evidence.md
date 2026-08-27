# Implementation evidence — memory-bounded-indexed-scale

Date: 2026-08-24. Branch `phase8/indexed-scale-production-readiness-20260824`.

## Reproduced defects and root causes

1. **Eviction-breaking search correctness (blocking, fixed).**
   `IndexPipeline::SearchIndexed` called `Projection::ReloadAllEvictedVolumes(store_)`
   on every query. Measured effects: whole-volume RAM reconstruction per query
   after any eviction (ceiling violation); reload→`EnforceMemoryLimitNow`→evict
   thrash across alternating query/ingestion cycles; and, because the boolean
   result was discarded, a failed reload silently omitted durable rows from a
   response presented as complete. Fix: per-volume hot/cold partition with
   durable streaming (`Store::ForEachEntryUntil`), trigram gating, hybrid
   materialization (`Store::FindEntry` walks), fail-closed `Unavailable`,
   cancellation inside cold streams. Regression:
   `TestForcedEvictionSearchEquivalence` (13 query classes, reference-vs-evicted
   identity+order equality), `TestRepeatedColdSearchesDoNotThrashMemory`
   (25 cold searches → zero evictions, zero reloads), plus mutation/restart/
   aggregate/cancellation suites.
2. **Enumeration-order-dependent capped results (fixed).** Truncation happened
   pre-sort in scan order, so capped result sets changed when enumeration order
   changed (hot vs cold sources). Fix: shared strict total order
   (name→folder→volume→id; previously equal-key rows fell through to
   stable-sort input order) + bounded top-N heap selection in
   `ExecuteSearchCore`/`ExecuteSearchViews`; `truncated` now means "matches
   exceeded maxResults", not "scan stopped early".
3. **LRU-victim nondeterminism (test-design finding).** Budget-shrink eviction
   chose victims by last-access stamps that reference-pass materializations had
   touched, so the "small" fixture volume was evicted instead of the intended
   one. Addressed by the explicit `Projection::EvictVolume` /
   `IndexPipeline::UnloadVolumeFromProjection` policy hook (also useful for
   unloading offline volumes) used by the deterministic suites.

## Path/filesystem edge-case dispositions

- Covered deterministically (pre-existing + this change): long paths,
  junction/reparse loops (depth-bounded), special entries in degraded
  enumeration, unicode filenames end-to-end (store→search→snapshot),
  duplicate basenames with deterministic ordering, case-insensitive matching,
  offline/unavailable volumes (queryable-while-marked per spec),
  disappearing volume via `RemoveVolumeDirectories`, stale-key cleanup.
- External/unavailable in this environment: real UNC share roots, removable
  media insertion/removal, drive-letter reuse races, multi-host scenarios.
  These require hardware/network fixtures and are recorded as not exercised;
  no fabricated results.

## Validation gates (this session, this branch)

| Gate | Result |
| --- | --- |
| Debug build (/W4 /WX) | warning-clean |
| Debug CTest | **74/74 PASS** |
| release-test configure/build | clean |
| release-test CTest | **74/74 PASS** |
| analyze configure/build (/analyze) | warning-clean (after C6011 lambda guard) |
| Historical ASAN protocol + IPC fuzz baseline | **PASS**; the current authority is the three-target protocol/IPC/lifecycle slice |
| Strict OpenSpec validate --all | **60/60 PASS** |
| ffdoc_drift_tests (superseded claims + line endings) | PASS |
| check_source_markers.ps1 | PASS (210 files) |
| check_diagnostic_policy.ps1 | PASS (12 checks) |

## Scale evidence

See `docs/benchmarks/phase8-large-index-scalability.md` (+ JSON at 100K/500K/1M
and 2M). Headline: hot-vs-cold result counts identical for every query class at
every scale; 1M-row rebuild 6.3 s; warm capped broad median 1.71 s at 1M; cold
streams are linear and cancellable; projection drops to 0.3–3% of full
residency under forced unload (e.g. 403.9 MB → 12.6 MB at 1M); snapshot
capacity probe ≈2.2M rows at the 256 MiB fail-closed cap while ordinary
publication is bounded by the 4,096-directory working set.

## Production activation status

Production signer, authorized pins, signed install/runtime, and elevated
clean-host MFT/USN matrix remain **REQUIRED-BUT-UNAVAILABLE** (external).
Nothing in this change weakens Authenticode verification, pin evaluation, or
the diagnostic escape hatch's Debug-only scope (policy gate re-run PASS). The
seeded acceptance suite exercises the architecture above the acquisition
boundary only and fabricates no production evidence.
