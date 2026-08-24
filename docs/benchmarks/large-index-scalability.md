# FastFiles large-index scalability baseline

> **Superseded for architecture claims (2026-08-24):** indexed search now has a
> durable cold path under projection eviction and publication is bounded by a
> working set. Current-architecture measurements live in
> [phase8-large-index-scalability.md](phase8-large-index-scalability.md). This
> document is retained as the pre-Phase-8 provenance baseline; its hot-path
> numbers remain representative for fully-resident indexes.

Reference run: 2026-08-10, Release, MSVC 19.44.35228.0, CMake 3.31.6, Windows 10.0.26200, AMD Ryzen 5 7535HS (12 logical cores), 23.3 GiB RAM, Kingston OM8SEP4512Q-AA NVMe SSD, NTFS C:. The machine-readable run is [large-index-scalability.json](large-index-scalability.json).

No formal latency target was found in the repository. The interpretation below uses engineering bands only: `<50 ms` immediate, `50–100 ms` excellent, `100–250 ms` responsive, `250–500 ms` noticeable, `>500 ms` poor, and `>1 s` not instant.

## Reproduction

```powershell
# Run from a Visual Studio developer environment. The verification harness
# discovers the installed VC toolchain and exposes resolved CMake/Ninja/CTest
# front ends from PATH or the selected installation.
cmake --preset release
cmake --build --preset release --target ffengine_bench_large_index_scalability
build\release\tests\engine\ffengine_bench_large_index_scalability.exe --output docs/benchmarks/large-index-scalability.json
```

The benchmark is not registered with CTest. It creates a temporary SQLite database per scale, opens it through `IndexPipeline`, rebuilds the real projection, runs the real indexed-search path, measures the real snapshot export/publication path, and removes the temporary directory afterward. Default scales are 10K, 100K, 500K, and 1M; larger scales are explicit (`--scales 2000000,5000000`) because the measured lower-scale boundary is already clear.

## Actual pipelines

Indexed search:

```text
SearchPanel::Dispatch
→ EngineClient::RequestIndexedSearch
→ UiServer::HandleRequestIndexedSearch
→ IndexPipeline::SearchIndexed
→ SQLite volume metadata lookup only
→ projection candidate/path materialization
→ ParseQuery
→ ExecuteSearch linear scope/filter/match scan + SortResults
→ response materialization
→ indexed-search response serialization / pipe
→ UI response parse + result materialization
```

The indexed search specification intentionally forbids a per-search SQLite entry query. The measured SQLite stage is therefore the small `GetAllVolumes` metadata query; entry matching is entirely projection-based. The request is clamped to 4,096 engine results, and the 1 MiB frame budget can reduce the wire result count further for long paths.

Snapshot publication:

```text
Projection::ForEachEntry / IndexPipeline::ExportDirectorySnapshot
→ SerializeSnapshot
→ SnapshotPublisher inactive-slot copy + generation commit
→ shared-section mapping + ParseSnapshot (UI-side proxy)
```

## Search medians

Warm-run medians; p90 and max are in the JSON. The query classes were selective (`unique_component_7391`), moderate (`report`), broad (`common`), no-match, prefix (`photo`), extension (`ext:txt`), and worst plausible substring (`component`).

| Index | Selective (1 result) | Broad | No match | Extension |
| ---: | ---: | ---: | ---: | ---: |
| 10K | 9.93 ms | 12.49 ms | 9.91 ms | 12.47 ms |
| 100K | 145.07 ms | 145.41 ms | 145.19 ms | 143.10 ms |
| 500K | 923.44 ms | 895.94 ms | 932.91 ms | 894.29 ms |
| 1M | 2,037.15 ms | 1,983.53 ms | 2,098.81 ms | 1,986.99 ms |

The nearly identical selective, broad, and no-match latencies show that total index size—not returned result count—is dominant in the current indexed path. At 1M, broad/extension queries requested 4,096 engine results but the wire parser received 3,955/4,001 because the response approached the 1 MiB frame budget; this is a secondary bounded-result issue.

## Representative 1M attribution

Warm selective query, one result, 2,037.15 ms total engine time:

| Stage | Time | Share |
| --- | ---: | ---: |
| Projection candidate/path materialization | 1,970.48 ms | 96.7% |
| SQLite metadata query | 33.81 ms | 1.7% |
| Match/filter/sort | 32.85 ms | 1.6% |
| Query parse | 0.005 ms | <0.1% |
| Response materialization | 0.002 ms | <0.1% |
| Response serialization | 0.008 ms | <0.1% |

The broad 1M case had the same shape: 1,952.96 ms materialization, 34.79 ms metadata SQL, 4.10 ms match/filter/sort, 0.73 ms response materialization, and 1.05 ms response serialization. Headless UI response parsing/materialization was 0.54/0.49 ms for the wire-truncated 3,955-row result.

## Actual snapshot export and publication

| Index | Export | Entries | Serialized bytes | Serialize | Publish copy/commit | UI map/parse | Outcome |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 10K | 1.84 ms | 9,999 | 1.10 MB | 1.64 ms | 0.42 ms | 1.56 ms | success |
| 100K | 32.03 ms | 99,999 | 11.25 MB | 17.72 ms | 3.95 ms | 18.67 ms | success |
| 500K | 194.11 ms | 499,999 | 57.37 MB | 98.66 ms | 19.57 ms | 100.65 ms | success |
| 1M | 378.76 ms | 999,999 | 115.16 MB | 178.15 ms | 0 ms | 0 ms | rejected oversize |

The isolated snapshot fixture measured approximately 120.6 serialized bytes per row at 500K. Its 500K run took 54.88 ms to build the model, 77.75 ms to serialize, 16.96 ms to publish, and 79.22 ms to map/parse.

## Capacity boundary

Configured slot capacity is 67,108,864 bytes (64 MiB). In the isolated fixture, 556,454 rows serialized to 67,108,794 bytes and published successfully. 556,455 rows serialized to 67,108,915 bytes, returned failure, and left the shared generation unchanged (`14 → 14`). The existing publication safety behavior is preserved; capacity was not increased.

## Memory and interactive typing

| Index | Private after rebuild | Private after broad query | Private retained after queries |
| ---: | ---: | ---: | ---: |
| 10K | 8.7 MB | 8.3 MB | 8.8 MB |
| 100K | 51.2 MB | 53.7 MB | 53.8 MB |
| 500K | 242.2 MB | 320.0 MB | 336.0 MB |
| 1M | 478.4 MB | 678.6 MB | 722.5 MB |

The 1M fixture database was 71.1 MB. No unbounded leak was proven by this short run, but per-query candidate copies add substantial retained allocator/process memory at scale.

At 1M, incremental `p → ph → pho → phot → photo` searches returned 4,096 rows at every step and measured 1,979.76–1,996.67 ms median per request. Every keystroke therefore performs the same full expensive indexed work.

Supersession is generation-safe but not cancellable on the indexed path: UI generation checks discard stale completions, while the engine completes old work and sends its response. A broad old request can therefore consume CPU/memory while a newer selective request waits or competes; no result-order correctness defect was found in the inspected path.

## Path-materialization optimization — historical stage

The 2026-08-10 follow-up keeps the historical baseline above and adds the optimized Release rerun. The resulting path is:

```text
query parse
→ SQLite volume metadata lookup
→ lightweight projection candidate/name/metadata copy
→ cheap filename/metadata predicates
→ lazy full-folder reconstruction only for folder-aware scope/filter survivors or matched results
→ canonical scope/filter/match/sort with the existing 4,096 cap
→ response materialization / serialization
```

The `folder:` predicate and non-all scopes still receive full canonical paths. `AllIndexedLocations` filename/metadata queries do not reconstruct paths for non-matches. Sorting and the cap remain in the existing matcher; unresolved paths are discarded before consuming the cap.

### Required before/after medians

| Scale | Query | Before Median | After Median | Speedup |
| ----: | ----- | ------------: | -----------: | ------: |
| 100K | Selective | 145.07 ms | 46.00 ms | 3.15x |
| 500K | Selective | 923.44 ms | 245.44 ms | 3.76x |
| 1M | Selective | 2,037.15 ms | 470.61 ms | 4.33x |
| 1M | No match | 2,098.81 ms | 472.99 ms | 4.44x |
| 1M | Broad | 1,983.53 ms | 441.56 ms | 4.49x |

Full query-class medians are ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:

| Scale | Before (ms) | After (ms) |
| ----: | -----------: | ----------: |
| 10K | 9.93 / 9.84 / 12.49 / 9.91 / 11.16 / 12.47 / 11.54 | 1.68 / 3.74 / 7.41 / 1.73 / 4.01 / 7.23 / 6.63 |
| 100K | 145.07 / 148.77 / 145.41 / 145.19 / 147.24 / 143.10 / 143.46 | 46.00 / 53.47 / 43.93 / 40.64 / 48.49 / 52.04 / 46.68 |
| 500K | 923.44 / 925.13 / 895.94 / 932.91 / 896.63 / 894.29 / 889.99 | 245.44 / 228.69 / 224.66 / 227.59 / 220.23 / 213.83 / 221.02 |
| 1M | 2,037.15 / 1,998.47 / 1,983.53 / 2,098.81 / 1,988.56 / 1,986.99 / 1,993.35 | 470.61 / 446.99 / 441.56 / 472.99 / 449.72 / 446.91 / 447.06 |

### Stage attribution — 1M selective

| Stage | Before | After | % After |
| ----- | -----: | ----: | ------: |
| Full projection parent-path reconstruction | 1,970.48 ms* | 0.014 ms | <0.1% |
| Lightweight name/metadata candidate materialization | included above* | 411.42 ms | 86.6% |
| SQLite metadata query | 33.81 ms | 33.10 ms | 7.0% |
| Match/filter/sort | 32.85 ms | 30.87 ms | 6.5% |
| Query parse | 0.0055 ms | 0.01 ms | <0.1% |
| Response materialization | 0.0021 ms | 0 ms | <0.1% |
| Total | 2,037.15 ms | 475.22 ms | 100% |

`*` The historical implementation measured candidate construction and full parent paths as one stage. The instrumented optimized rerun measured 411.42 ms for lightweight name/metadata candidates, 0.014 ms for one surviving lazy path, and 475.22 ms total. The new bottleneck is therefore candidate name conversion/copying and the linear filename predicate scan, not full parent-path reconstruction.

### Incremental search — 1M

| Query | Before | After | Speedup |
| ----- | -----: | ----: | ------: |
| `p` | 1,996.67 ms | 445.45 ms | 4.48x |
| `ph` | 1,990.67 ms | 448.34 ms | 4.44x |
| `pho` | 1,986.35 ms | 446.03 ms | 4.45x |
| `phot` | 1,990.34 ms | 444.62 ms | 4.48x |
| `photo` | 1,979.76 ms | 446.84 ms | 4.43x |

### Memory — 1M private bytes

| Measurement | Before | After |
| ----------- | -----: | ----: |
| Database/index open | 6.0 MB | 5.5 MB |
| Selective peak | 601.3 MB | 540.4 MB |
| Broad peak | 647.1 MB | 537.0 MB |
| Post-query retained | 689.1 MB | 530.5 MB |

The optimized run preserved the snapshot capacity boundary and supersession behavior; snapshot serialization/publication and cancellation were out of scope for this historical slice. The later audit-remediation work measures and implements both concerns separately.

## Assessment and decision

The first clear unacceptable boundary is between 100K and 500K: 100K is noticeable/responsive only at the upper edge, 500K is poor, and 1M is not instant. 2M/5M were not run because the measured 500K/1M failure boundary already determines the next engineering decision and larger runs would add little attribution value before optimization.

`PRIMARY BOTTLENECK BEFORE: projection candidate/path materialization in IndexPipeline::SearchIndexed`

`PRIMARY BOTTLENECK AFTER: lightweight candidate name/metadata materialization and the linear filename predicate scan`

`OPTIMIZATION REQUIRED: COMPLETED FOR THIS SLICE`

`NEXT SINGLE OPTIMIZATION SLICE: reduce per-entry name conversion/copying with a projection-backed candidate view or exact filename prefilter, guided by the same benchmark; cancellation was deferred at the time of this historical slice and is now covered by the audit-remediation cancellation work.`

This historical path-materialization slice intentionally did not implement the next candidate/name optimization.

## Candidate/name materialization optimization — final 2026-08-10 result

This slice preserves the original baseline and the completed path-materialization optimization above, then removes the remaining all-row owning candidate/name construction. The final path is:

```text
query parse
→ SQLite volume metadata lookup
→ streaming projection row/view traversal
→ direct UTF-16 filename/metadata predicates
→ lazy full-folder reconstruction only for scope/folder survivors or accepted results
→ owning SearchResult materialization only for accepted rows
→ existing ordering, cap-before-sort, response materialization, and serialization
```

The projection lock protects `NamePool` view lifetimes. Built-in predicates have view equivalents; custom predicates without a view factory retain the existing owning fallback. No query syntax, ordering, identity, scope, or result-cap behavior changed.

### Before/after medians for this slice

| Index | Query | Previous Median | New Median | Speedup |
| ----: | ----- | --------------: | ---------: | ------: |
| 100K | Selective | 46.00 ms | 17.88 ms | 2.57x |
| 500K | Selective | 245.44 ms | 132.88 ms | 1.85x |
| 1M | Selective | 470.61 ms | 293.18 ms | 1.61x |
| 1M | No match | 472.99 ms | 300.96 ms | 1.57x |
| 1M | Broad | 441.56 ms | 58.41 ms | 7.56x |

Full query-class medians are ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:

| Scale | Previous (ms) | New (ms) |
| ----: | ------------: | --------: |
| 10K | 1.68 / 3.74 / 7.41 / 1.73 / 4.01 / 7.23 / 6.63 | 0.68 / 2.75 / 6.42 / 0.73 / 2.88 / 6.07 / 4.81 |
| 100K | 46.00 / 53.47 / 43.93 / 40.64 / 48.49 / 52.04 / 46.68 | 17.88 / 21.87 / 14.55 / 18.67 / 21.79 / 13.37 / 16.07 |
| 500K | 245.44 / 228.69 / 224.66 / 227.59 / 220.23 / 213.83 / 221.02 | 132.88 / 46.48 / 36.36 / 135.76 / 46.82 / 35.30 / 38.89 |
| 1M | 470.61 / 446.99 / 441.56 / 472.99 / 449.72 / 446.91 / 447.06 | 293.18 / 69.74 / 58.41 / 300.96 / 69.91 / 58.60 / 61.17 |

### Stage attribution — 1M selective

| Stage | Previous | New | % New |
| ----- | -------: | ---: | -----: |
| Full parent-path reconstruction | 0.014 ms | 0.008 ms | <0.1% |
| Lightweight owning candidate/name work | 411.42 ms | 0.003 ms | <0.1% |
| Projection traversal + direct matching/filter/sort | 30.87 ms | 271.79 ms | 89.1% |
| SQLite metadata query | 33.10 ms | 33.24 ms | 10.9% |
| Query parse | 0.010 ms | 0.007 ms | <0.1% |
| Response materialization | 0.002 ms | 0.001 ms | <0.1% |
| Total sampled run | 475.22 ms | 305.05 ms | 100% |

The sampled new total is higher than the 293.18 ms warm median because the table uses one instrumented run for attribution. The 411 ms all-row candidate/name stage is removed; the new dominant work is raw projection traversal plus the linear direct filename predicate scan.

### Allocation/materialization counts — 1M selective

| Item | Previous | New |
| ---- | -------: | ---: |
| Projection rows examined | 1,000,000 | 1,000,000 |
| Owning candidate objects | 1,000,000 | 1 |
| Owning name copies | 1,000,000 | 1 |
| Path materializations | 1 | 1 |
| SearchResult objects | 1 | 1 |

Broad queries retain only the accepted capped set: the final run examined 13,779 rows before the exact 4,096 cap, materialized 4,096 owning candidates/names/results, and preserved the historical wire-truncated 3,955-row response.

### Incremental search — 1M

| Query | Previous | New | Speedup |
| ----- | -------: | ---: | ------: |
| `p` | 445.45 ms | 55.44 ms | 8.03x |
| `ph` | 448.34 ms | 70.53 ms | 6.36x |
| `pho` | 446.03 ms | 70.85 ms | 6.29x |
| `phot` | 444.62 ms | 71.04 ms | 6.26x |
| `photo` | 446.84 ms | 70.41 ms | 6.35x |

### Memory — 1M private bytes

| Measurement | Previous | New |
| ----------- | -------: | ---: |
| Database/index open | 5.5 MB | 6.0 MB |
| Selective peak | 540.4 MB | 466.3 MB |
| Broad peak | 537.0 MB | 473.9 MB |
| Post-query retained | 530.5 MB | 475.3 MB |

### Semantic equivalence

Focused deterministic engine/search coverage remains passing for selective, no-match, broad/capped, filename, parent-path, extension, prefix/glob, case-insensitive, multi-term, nested duplicate names, path ordering, folder predicates, current-folder scope, Unicode/locale-independent matching, surrogate-pair wildcard behavior, and disabled/unavailable-volume boundaries. The view matcher is directly checked against the owning matcher for built-in predicates.

`PRIMARY BOTTLENECK AFTER CANDIDATE OPTIMIZATION: raw projection traversal plus direct UTF-16 filename matching/filtering (271.79 ms, 89.1% of the sampled 1M selective search)`

`NEXT SINGLE OPTIMIZATION SLICE: reduce projection iteration/name-predicate scan cost, guided by dense live-entry traversal or a narrowly measured exact filename prefilter; SQLite schema work and cancellation remain separate.`

The candidate/name materialization optimization is complete; this benchmark does not implement the next slice.

## Projection scan / direct filename matching optimization — final 2026-08-10

This slice preserves the preceding original → path-materialization → candidate/name-materialization history. It optimizes the remaining projection walk without changing the query language, owning-result boundary, lazy paths, ordering, or cap-before-sort behavior.

### Root cause and broad/selective explanation

The old 1M selective stage was approximately `271.79 ms` (`89.1%` of the sampled search). Detailed diagnostics decomposed it into overlapping work: about `102.6 ms` of NamePool lookup timing, `23.2 ms` of name-view construction timing, and `63.7 ms` of direct predicate/substring timing; the combined stage is the authoritative wall-clock measure. No-match traversed the same 1,000,000 rows and performed `2,872,240` ordinal comparison windows. Broad was different because the existing cap-before-sort behavior stopped after exactly 4,096 accepted results, at 13,779 examined rows; it did not scan the full million.

The final implementation makes rebuilt projection entries follow the original live hash-bucket order in a dense vector and remaps the ID and parent-child indices. A valid dense-order flag is used for read-only scans; after mutations, direct live-slot iteration preserves the previous order. The matcher recognizes a single simple literal name term, folds query-side ASCII once, stores only a one-bit-equivalent entry property indicating an ASCII lowercase-compatible name, and uses an allocation-free anchor search in that case. Uppercase/non-ASCII names and complex terms retain the canonical ordinal/Unicode fallback. No lowercase filename copies are stored.

### Final search pipeline

```text
query parse and query-side literal preparation
→ SQLite volume metadata lookup
→ projection lock and dense live-entry/view traversal
→ prepared simple-literal matcher or canonical generic view predicates
→ lazy folder/path materialization only for survivors
→ owning result materialization, existing 4,096 cap, and sort
→ response serialization
```

NamePool access remains a UTF-16 view. Simple literal queries use the prepared query term and an ASCII anchor when the entry property proves the exact fast-path precondition; all other cases use the existing ordinal case-insensitive semantics. Result and path ownership remain limited to accepted rows and path-dependent predicates.

### Before/after medians

| Index | Query | Before | After | Speedup |
| ----: | ----- | -----: | ----: | ------: |
| 100K | Selective | 17.88 ms | 7.40 ms | 2.42x |
| 500K | Selective | 132.88 ms | 57.29 ms | 2.32x |
| 1M | Selective | 293.18 ms | 130.76 ms | 2.24x |
| 1M | No match | 300.96 ms | 172.52 ms | 1.74x |
| 1M | Broad | 58.41 ms | 51.09 ms | 1.14x |
| 1M | Worst substring | 61.17 ms | 52.87 ms | 1.16x |
| 1M | Extension | 58.60 ms | 51.74 ms | 1.13x |

Full query-class medians remain in the machine-readable artifact. Ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:

| Scale | Before (ms) | After (ms) |
| ----: | -----------: | ----------: |
| 10K | 0.68 / 2.75 / 6.42 / 0.73 / 2.88 / 6.07 / 4.81 | 0.49 / 2.24 / 5.35 / 0.49 / 2.28 / 4.91 / 3.83 |
| 100K | 17.88 / 21.87 / 14.55 / 18.67 / 21.79 / 13.37 / 16.07 | 7.40 / 15.87 / 12.27 / 7.23 / 16.40 / 12.42 / 13.40 |
| 500K | 132.88 / 46.48 / 36.36 / 135.76 / 46.82 / 35.30 / 38.89 | 57.29 / 37.84 / 32.62 / 73.78 / 39.68 / 32.42 / 32.63 |
| 1M | 293.18 / 69.74 / 58.41 / 300.96 / 69.91 / 58.60 / 61.17 | 130.76 / 59.07 / 51.09 / 172.52 / 60.31 / 51.74 / 52.87 |

### Stage attribution — 1M selective

The normal warm sample below is paired with a separate projection-only probe. Diagnostic per-operation clocks are intentionally non-additive and perturb wall time; they are included to identify operations, not to replace the normal median.

| Stage | Before | After | % After |
| ----- | -----: | ----: | ------: |
| SQLite metadata | 33.24 ms | 33.98 ms | 26.4% |
| Projection-only traversal floor | not isolated | 54.28 ms | 42.1% |
| Direct literal matching/filter residual | included in 271.79 ms | ~40.54 ms | 31.5% |
| Combined projection traversal + matching | 271.79 ms | 94.82 ms | 73.6% |
| Lazy path work | 0.008 ms | 0.012 ms | <0.1% |
| Result handling | 0.003 ms | 0.016 ms | <0.1% |
| Total sampled search | 305.05 ms | 128.81 ms | 100% |

The normal final 1M selective median is `130.76 ms`. The paired sample examined 1,000,000 rows, performed 999,999 NamePool lookups and 1,000,000 view/predicate evaluations, accepted one row, and materialized one candidate/name/result with one lazy path. The no-match warm sample measured `138.01 ms` for the combined projection/matching stage; subtracting the `54.28 ms` projection-only floor leaves an approximately `83.73 ms` failed-literal matching/filter residual. Its diagnostic run counted 1,000,000 failed substring searches, 1,000,000 early rejects, and zero path lookups. This is the remaining dominant operation.

### Incremental search — 1M

| Query | Before | After | Speedup |
| ----- | -----: | ----: | ------: |
| `p` | 55.44 ms | 49.99 ms | 1.11x |
| `ph` | 70.53 ms | 59.55 ms | 1.18x |
| `pho` | 70.85 ms | 58.74 ms | 1.21x |
| `phot` | 71.04 ms | 58.53 ms | 1.21x |
| `photo` | 70.41 ms | 59.18 ms | 1.19x |

### Memory and semantic equivalence

The clean isolated 1M run measured private memory at approximately `4.4 / 454.0 / 470.4 / 471.2 MiB` for open / rebuild / after broad / post-query, versus the prior `6.0 / 466.3 / 473.9 / 475.3 MiB`. The canonical multi-scale artifact records `6.4 / 455.2 / 487.7 / 490.1 MiB`; the higher post-broad values are allocator-history effects from retaining the earlier 10K/100K/500K runs in the same process, not persistent projection storage. The isolated measurement is the comparable footprint check and shows no material regression.

| Scenario | Result |
| -------- | ------ |
| selective | PASS |
| no match | PASS |
| broad / capped | PASS |
| exact 4,096 result set | PASS |
| filename substring | PASS |
| parent path | PASS |
| extension | PASS |
| prefix | PASS |
| glob | PASS |
| ASCII case-insensitive | PASS |
| Unicode/non-ASCII | PASS |
| multi-term | PASS |
| metadata/folder predicate | PASS |
| duplicate names | PASS |
| result ordering | PASS |

Focused deterministic search tests pass, including new uppercase-ASCII fast-path/fallback agreement and non-ASCII Unicode fallback cases. Existing engine tests continue to cover parent-path-only queries, folder scope, glob, extension, metadata, duplicate names, ordering, and the 4,096 cap.

### Larger scale

2M: NOT TESTED
5M: NOT TESTED

### Assessment and next slice

`PRIMARY BOTTLENECK BEFORE: combined raw projection traversal plus direct UTF-16 matching/filtering (271.79 ms sampled)`

`PRIMARY BOTTLENECK AFTER: failed direct literal filename matching/filtering (~83.73 ms residual in the 1M no-match sample)`

`NEXT SINGLE OPTIMIZATION SLICE: evaluate an evidence-backed secondary filename-search index/FTS design or a narrowly targeted failed-literal matcher improvement; do not implement it in this slice.`

At the time of this historical artifact, cancellation, snapshot changes, SQL schema redesign, MFT/USN activation, and security/signing work remained deferred. The current repository contains subsequent audit-remediation work for cancellation and snapshot publication; the historical measurements below are retained for attribution.

## Failed literal filename matcher optimization

This follow-up slice measured and optimized only failed direct literal filename matching. The old simple-literal path pre-folded an ASCII query once, selected a rare anchor, scanned each ASCII-compatible name with `u16string_view::find` plus folded-view equality, and used a per-window `CompareStringOrdinal(..., TRUE)` fallback for uppercase/non-ASCII cases. It already rejected a needle longer than the candidate name before searching.

The isolated 1M matcher benchmark used the real synthetic filename distribution, plus short/long, late-match, common-prefix, and Unicode cases:

| Strategy | Failed | Early | Late | Short 2–4 | Long | Common prefix | Unicode fallback |
| -------- | -----: | ----: | ---: | --------: | ----: | ------------: | ----------------: |
| Current anchor | 19.1 ms | 22.7 ms | 26.6 ms | 25.5 ms | 16.0 ms | 21.7 ms | exact fallback |
| First + last rejection | 16.8 ms | 23.6 ms | 22.7 ms | 22.5–25.0 ms | 15.2 ms | 46.6 ms | exact fallback |
| BMH candidate | 14.5 ms | 21.0 ms | 25.0 ms | 25.5–35.0 ms | 12.6 ms | 16.5 ms | exact fallback |

The selected implementation stores a 128-entry BMH shift table in the prepared `Query`. It is used only for ASCII-compatible names and folded needles of length 4 or more; 1–3 character needles retain the existing anchor path, and all non-ASCII/uppercase cases retain the exact ordinal fallback. The no-diagnostics path also avoids the benchmark diagnostics scope. No persistent lowercase filename copy or secondary index was added.

### Final search evidence

| Scale | Query | Before | After | Speedup |
| ----: | ----- | -----: | ----: | ------: |
| 100K | Selective | 7.23 ms | 6.59 ms | 1.10x |
| 500K | Selective | 72.46 ms | 58.33 ms | 1.24x |
| 1M | Selective | 131.06 ms | 126.99 ms | 1.03x |
| 1M | No match | 166.33 ms | 147.90 ms | 1.12x |
| 1M | Broad | 48.83 ms | 48.82 ms | 1.00x |
| 1M | Worst substring | 50.92 ms | 49.95 ms | 1.02x |
| 1M | Extension | 50.27 ms | 49.30 ms | 1.02x |

Final 1M p90/max were selective `129.34/130.71 ms`, no-match `149.38/152.35 ms`, broad `49.99/50.73 ms`, and worst-substring `50.13/51.20 ms`. Incremental `p/ph/pho/phot/photo` was `46.97/56.26/56.38/55.62/55.70 ms` median; cancellation was intentionally a separate future concern for this historical slice and is now addressed by the audit-remediation work.

### Attribution and decision

Using the established projection-only floor (`54.28 ms`) and SQLite metadata floor (`33.98 ms`), the 1M no-match failed-literal residual is approximately `59.64 ms` after this slice, versus approximately `83.73 ms` before it. The combined lower bound is already about `88 ms` before meaningful result work. The full scan remains linear in projection size and still costs `147.90 ms` for a guaranteed no-match query; a secondary filename-search design must therefore be evaluated before further matcher micro-optimization.

`ARCHITECTURAL DECISION: SECONDARY FILENAME INDEX EVALUATION REQUIRED`

`NEW PRIMARY BOTTLENECK: full O(N) projection traversal plus residual literal rejection`

## Secondary filename index integration — 2026-08-11

The measured UTF-16 trigram posting design is now integrated into the production indexed-search path. The new historical artifact is [large-index-scalability-secondary-index.json](large-index-scalability-secondary-index.json); the original `large-index-scalability.json` remains unchanged so the optimization history is preserved.

The current pipeline is:

```text
ParseQuery
→ GetAllVolumes metadata query
→ projection lock
→ eligible simple-literal UTF-16 trigram candidate lookup
→ canonical filename/path/predicate verification
→ existing result cap, ordering, and response
```

Only simple literal filename terms with at least three ASCII-foldable UTF-16 units use the accelerator. Short terms, Unicode terms, globs, multi-term queries, path predicates, extension predicates, metadata predicates, and any unavailable/invalid index use the existing canonical scan. The index is a candidate generator, never the semantic authority; exact result sets and ordering remain owned by the canonical matcher.

### Integrated Release medians

| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 1.93 ms | 2.02 ms | 10.99 ms |
| 500K | 17.71 ms | 18.06 ms | 29.15 ms |
| 1M | 34.34 ms | 36.25 ms | 47.83 ms |
| 2M | 64.91 ms | 67.84 ms | 81.36 ms |

At 2M, the selective query verified one candidate and the no-match query verified zero candidates; both still measured approximately 65–68 ms because the `GetAllVolumes` SQLite metadata query is now the dominant stage. Broad search used 590,624 candidates and retained the existing 4,096-result cap. The query summaries include p90/max values and all seven benchmark classes.

The integrated result improves the prior same-harness 1M medians of approximately `126.99/147.90/48.82 ms` (selective/no-match/broad) to `34.34/36.25/47.83 ms`. Selective and no-match no longer scale with the full projection row count; broad remains governed by candidate verification and result materialization.

### Lifecycle and resource behavior

`Projection` owns the postings beside its generation-owned dense slots. Rebuilds bulk-load and sort/deduplicate postings after compaction; insert, delete, and rename update postings incrementally. Allocation or update failure invalidates only the accelerator, and search falls back to the canonical linear path. Focused tests cover duplicate/uppercase/non-ASCII recall, empty no-match, short and Unicode fallback, delete, and rename behavior; engine tests cover eligible, no-match, short-term, and folder-query routing.

The final artifact reports private bytes after rebuild of approximately `59.3/280.7/551.9/1120.6 MiB` at `100K/500K/1M/2M`, with post-query values of `61.3/292.7/565.4/1294.2 MiB`. The isolated architecture benchmark estimated 168.6 MiB of trigram postings at 2M; the integrated process also retains the existing SQLite/projection/snapshot working set. The 2M rebuild took approximately 5.0 seconds and the temporary database occupied 150,564,864 bytes. No schema migration or persistent secondary-index file was introduced.

This historical slice did not optimize SQLite or add search cancellation. Those were separate decisions requiring fresh measurements rather than attribution from the pre-index path; the current audit-remediation work adds cancellation with dedicated regression and latency coverage.

No 2M run was performed because the 1M selective result remained above the optional scale-up gate. An isolated 1M memory check measured approximately `4.9 / 453.8 / 468.8 / 470.1 MiB` for open / rebuild / after broad / post-query, with no material regression against the prior isolated `4.4 / 454.0 / 470.4 / 471.2 MiB` check. Focused and full semantic tests passed for selective/no-match, early/late matching, short and overlong needles, ASCII case-insensitivity, uppercase fallback, Unicode/mixed Unicode, prefix, glob, extension, parent path, multi-term, duplicate names, capped 4,096 results, and ordering.

## SQL metadata no-count path — 2026-08-11

The secondary-index artifact showed that `GetAllVolumes()` had become the dominant stage because it counted every entry for every volume even though indexed search only needs volume identity, availability, and the resolved root key. The focused change adds `GetAllVolumesWithoutEntryCounts()` and routes both indexed-search branches through it. Rebuild, public metadata, and unavailable-volume reporting retain the counted API and its existing semantics; no schema migration or entry-count cache was added.

The final historical artifact is [large-index-scalability-sql-no-count.json](large-index-scalability-sql-no-count.json). It preserves the previous secondary-index artifact and records the same Release harness at 100K/500K/1M/2M:

| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 0.03 ms | 0.06 ms | 8.82 ms |
| 500K | 0.50 ms | 0.62 ms | 11.89 ms |
| 1M | 0.64 ms | 0.63 ms | 14.29 ms |
| 2M | 0.55 ms | 2.24 ms | 17.55 ms |

At 2M, the eligible selective query still used one trigram candidate and the no-match query used zero; SQLite metadata timing was approximately `0.10 ms`, down from approximately `65–68 ms`. Broad search retained the existing 4,096-result cap and used 590,624 candidates. The result is now below the low-tens-of-milliseconds engineering target for eligible selective/no-match queries through 2M; no further SQL redesign is justified by this measurement.

The benchmark process exited after writing the complete artifact when the external 120-second command deadline expired; post-run checks found no live benchmark process, no leftover disposable benchmark directories, and valid JSON with 4 datasets, 179 runs, and 28 summaries. Focused Store/engine tests passed before the benchmark. Full Debug/Analyze/CTest validation and the bounded product commit remain the next gate for this slice.

## Phase 9 non-blocking read-plane evidence — 2026-08-24

The benchmark now records `search_generation`, generation restarts, state-lock
wait/maximum hold time, peak searches/readers, cumulative read-plane counters,
and a deterministic contention probe. The probe uses a real cold query parked
at the durable-stream boundary. While it is parked, the existing pipeline
executes an MFT ingestion batch, a durable folder aggregate, a directory
snapshot export, and a second indexed search; the first query is then
cancelled and its reader is released. These are evidence fields, not brittle
latency gates.

The first Phase 9 run used the established 10K fixture on the same host and
compared the current refactor with the immediately preceding Phase 8 binary:

| 10K case | Phase 8 | Phase 9 | Interpretation |
| --- | ---: | ---: | --- |
| warm selective | 0.64 ms | 0.48 ms | same-harness reference; no fast-path regression observed |
| cold broad | 806.99 ms | 742.34 ms | cold streaming remains expensive but is isolated from writers |

The Phase 9 contention probe recorded: ingestion `2.07 ms`, aggregate
`81.55 ms`, snapshot export `311.74 ms`, second query `365.62 ms`, all
completed while the first search was held for `365.98 ms`; cancellation took
`0.24 ms`. The probe reached the cold stream, reported two peak durable
readers, ended with zero active readers, and published no partial result.
The cumulative read-plane counters ended at two peak searches, 99 completed
searches, one cancellation, zero unavailable searches, and zero reader
failures. The 10K post-query private bytes were approximately 13.6 MiB, with
99 handles and four process threads; the WAL measured 20,632 bytes at the
post-query sample. The forced-eviction projection estimate remained
`4,297,568 → 196,072` bytes.

The harness still retains the 100K/500K/1M/2M default scales. A complete
multi-scale Phase 9 rerun is evidence work rather than a CTest gate; the
mandatory architectural result is already structural: a deterministic
long-running cold read no longer holds the mutation/lifecycle critical
section while ingestion, aggregate, snapshot, another search, and
cancellation make progress.

### Final rerun and regression investigation

Two final 10K runs used the same temporary-database harness and machine. The
earlier Phase 9 run remains the clean comparison point; the two final runs
were collected while the host also had several high-CPU agent/VM processes, so
their wall-clock values are recorded as noisy evidence rather than acceptance
thresholds:

| Case | Phase 9 reference | Final run A | Final run B |
| --- | ---: | ---: | ---: |
| warm selective median | 0.53 ms | 0.95 ms | 0.75 ms |
| warm broad average | 183.42 ms | 232.69 ms | 229.99 ms |
| cold selective | 124.78 ms | 150.43 ms | 190.56 ms |
| cold broad | 742.34 ms | 981.03 ms | 996.03 ms |

The stage counters show the variance is dominated by projection traversal and
durable stream wall time, not an unbounded synchronization hold: the final
warm-broad runs reported maximum state-lock holds of `17.12 ms` and
`15.88 ms`, with zero generation restarts; the cold-broad runs reported
`0.50 ms` and `0.56 ms` maximum state-lock holds. A process snapshot during
the rerun confirmed substantial unrelated host CPU contention. No semantic or
algorithmic regression was identified, and no brittle timing threshold was
added. The structural contention probe, bounded memory/readers, and exact
search assertions remain the Phase 9 acceptance evidence.
