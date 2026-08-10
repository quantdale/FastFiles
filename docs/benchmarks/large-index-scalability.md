# FastFiles large-index scalability baseline

Reference run: 2026-08-10, Release, MSVC 19.44.35228.0, CMake 3.31.6, Windows 10.0.26200, AMD Ryzen 5 7535HS (12 logical cores), 23.3 GiB RAM, Kingston OM8SEP4512Q-AA NVMe SSD, NTFS C:. The machine-readable run is [large-index-scalability.json](large-index-scalability.json).

No formal latency target was found in the repository. The interpretation below uses engineering bands only: `<50 ms` immediate, `50–100 ms` excellent, `100–250 ms` responsive, `250–500 ms` noticeable, `>500 ms` poor, and `>1 s` not instant.

## Reproduction

```powershell
$env:FASTFILES_NINJA_EXE = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
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

## Indexed-search materialization optimization follow-up

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

The optimized run preserved the snapshot capacity boundary and supersession behavior; snapshot serialization/publication and cancellation remain out of scope.

## Assessment and decision

The first clear unacceptable boundary is between 100K and 500K: 100K is noticeable/responsive only at the upper edge, 500K is poor, and 1M is not instant. 2M/5M were not run because the measured 500K/1M failure boundary already determines the next engineering decision and larger runs would add little attribution value before optimization.

`PRIMARY BOTTLENECK BEFORE: projection candidate/path materialization in IndexPipeline::SearchIndexed`

`PRIMARY BOTTLENECK AFTER: lightweight candidate name/metadata materialization and the linear filename predicate scan`

`OPTIMIZATION REQUIRED: COMPLETED FOR THIS SLICE`

`NEXT SINGLE OPTIMIZATION SLICE: reduce per-entry name conversion/copying with a projection-backed candidate view or exact filename prefilter, guided by the same benchmark; cancellation remains deferred.`

This benchmark slice intentionally does not implement that optimization.
