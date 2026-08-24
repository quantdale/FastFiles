# Phase 8 — memory-bounded indexed scale certification run

Reference run: 2026-08-24, Release, MSVC 19.44 (VS 2022 BuildTools), Windows,
AMD Ryzen 5 7535HS-class host (12 logical cores), ~23 GiB RAM, NVMe SSD, NTFS
`C:`. Machine-readable runs: [phase8-large-index-scalability.json](phase8-large-index-scalability.json)
(100K/500K/1M) and, when present, [phase8-large-index-scalability-2m.json](phase8-large-index-scalability-2m.json)
(2M). Reproduce with:

```powershell
cmake --build --preset release --target ffengine_bench_large_index_scalability
build\release\tests\engine\ffengine_bench_large_index_scalability.exe ^
  --scales 100000,500000,1000000 --output docs/benchmarks/phase8-large-index-scalability.json
```

This run supersedes the 2026-08-10 baseline in `large-index-scalability.md`
for *architecture* claims: indexed search is no longer purely
projection-resident. Since `memory-bounded-indexed-scale`, a searchable volume
that has been subtree-evicted streams its rows **durably** from SQLite
(`Store::ForEachEntryUntil`) under the same matcher/sorter/cap contract; the
filename-trigram accelerator is used only when every searched volume is
resident. The historical document is retained for provenance; its hot-path
numbers remain representative for fully-resident indexes.

## Index/store stage

| Scale | DB size | Fixture generation | Projection rebuild |
| ---: | ---: | ---: | ---: |
| 100,000 | 7.0 MB | 1.3 s | 0.7 s |
| 500,000 | 36.0 MB | 17.9 s | 3.7 s |
| 1,000,000 | 72.2 MB | 49.6 s | 6.3 s |

## Search medians (ms), warm = all rows projection-resident

| Query class | 100K | 500K | 1M | Results |
| --- | ---: | ---: | ---: | ---: |
| selective | 0.31 | 0.89 | 1.35 | 1 |
| moderate | 57.7 | 344.1 | 588.1 | 4,096 (capped) |
| broad | 150.6 | 934.9 | 1,705.4 | 4,096 (capped) |
| no_match | 0.14 | 1.57 | 3.95 | 0 |
| prefix | 58.2 | 306.1 | 597.7 | 4,096 (capped) |
| extension | 154.4 | 936.6 | 1,736.3 | 4,096 (capped) |
| worst_substring | 89.2 | 553.7 | 1,058.7 | 4,096 (capped) |

## Cold search after forced eviction (`UnloadVolumeFromProjection`)

Projection estimated bytes drop from the full-volume residency to a small
bounded residue, and every query then streams the durable store. Result counts
are **identical** to the warm run for every query class at every scale — this
is the hot/cold completeness contract exercised at scale.

| Query class | 100K cold ms (coldRows) | 500K cold ms | 1M cold ms |
| --- | ---: | ---: | ---: |
| selective | 174.4 (100K) | 1,501.7 | 1,675.0 |
| moderate | 775.5 | 7,087.7 | 7,965.1 |
| broad | 1,954.2 | 10,960.3 | 19,527.7 |
| no_match | 143.2 | 892.9 | 1,511.7 |
| prefix | 785.4 | 4,033.3 | 7,620.7 |
| extension | 2,000.1 | 9,816.4 | 18,970.6 |
| worst_substring | 1,214.4 | 6,831.8 | 13,199.0 |

Cold cost is a bounded, cancellable SQLite stream (linear in indexed rows);
repeated cold searches do not grow or thrash the projection (enforced by
`TestRepeatedColdSearchesDoNotThrashMemory`). Estimated projection bytes at
forced eviction: 41.7 MB → 1.6 MB (100K), 201.6 MB → 7.0 MB (500K),
403.9 MB → 12.6 MB (1M).

## Snapshot publication (256 MiB fail-closed format, v3)

The benchmark measures model/export time, serialized bytes, publish
copy+commit, and UI-side map/parse per scale (see the JSON `snapshot` arrays).
Capacity probe: the largest published snapshot observed was **1,980,421 rows**
against a theoretical single-generation capacity of ≈2,200,468 rows at
268,435,456 bytes. With the bounded working-set publication introduced in this
change, ordinary browsing publishes at most
`kInitialIndexedExportDirectories` (4,096) top-of-tree directories plus the
actively browsed/observed set — payload scales with the working set, and the
256 MiB ceiling now bounds an extreme multi-volume worst case rather than a
routine large-volume scan.

## 2M rows (clean rerun; canonical-path dominated — the trigram accelerator was
not available in this run's residency history, so "warm" here measures the
canonical scan; cold adds the durable stream on top)

| Query class | warm median ms | COLD ms | results |
| --- | ---: | ---: | ---: |
| selective | 2,305.3 | 2,374.2 | 1 |
| moderate | 3,157.4 | 12,737.8 | 4,096 |
| broad | 4,672.7 | 29,827.6 | 4,096 |
| no_match | 2,322.4 | 2,374.5 | 0 |
| prefix | 3,185.0 | 11,551.6 | 4,096 |
| extension | 4,389.1 | 30,912.3 | 4,096 |
| worst_substring | 3,802.0 | 27,026.6 | 4,096 |

DB 145.9 MB; generation 115.8 s; rebuild 20.9 s; forced eviction 903.3 MB →
26.2 MB estimated projection bytes.

## Interpretation

- Fully-resident search remains sub-second to ~1.7 s for capped broad queries
  at 1M rows; selective lookups are low-single-digit milliseconds.
- Cold (evicted) searches are complete but linearly slower; they exist so that
  correctness never depends on RAM. Interactive use keeps its working set
  resident via normal browse/rebuild behavior.
- No wall-clock number here is a CI gate; deterministic equivalence and
  policy tests cover correctness (`ffengine_index_pipeline_tests`,
  `ffindexstore_projection_tests`, `ffengine_seeded_indexed_mode_tests`).
