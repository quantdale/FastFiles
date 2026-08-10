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

## Assessment and decision

The first clear unacceptable boundary is between 100K and 500K: 100K is noticeable/responsive only at the upper edge, 500K is poor, and 1M is not instant. 2M/5M were not run because the measured 500K/1M failure boundary already determines the next engineering decision and larger runs would add little attribution value before optimization.

`PRIMARY BOTTLENECK: projection candidate/path materialization in IndexPipeline::SearchIndexed`

`OPTIMIZATION REQUIRED: YES`

`NEXT SINGLE OPTIMIZATION SLICE: reduce or eliminate the full projection candidate/path materialization performed for every indexed search, guided by a follow-up benchmark that preserves the current query semantics and result cap.`

This benchmark slice intentionally does not implement that optimization.
