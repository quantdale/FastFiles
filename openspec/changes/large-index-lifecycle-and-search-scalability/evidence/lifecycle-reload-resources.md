# Cold-volume reload (8.1) & repeated-cycle resource stability (8.2) — 2026-10-21

Harness: `ffengine_bench_large_index_scalability --memory-report
build/campaign/memory-report-10cycles.json --scales 200000,1000000
--rebuild-cycles 10 --source-state clean --source-sha b6df9ef --preset
release-test --diagnostics` (release-test binary at `b6df9ef`; console log
`build/campaign/memory-report-10cycles-console.log`). All product code paths:
real `IndexPipeline` + real `Store` + real `SnapshotPublisher`.

## 8.1 Cold-volume reload (the production reload path)

The production reload entry is `ExportDirectorySnapshotBoundedPooled` ->
`projection_.ReloadEvictedVolume` -> `RebuildVolumeFromStore` (full-volume
re-stream of `SELECT ... WHERE volume_id=?` + per-row Upsert +
`RebuildFilenameIndex` + `ShrinkBulkRebuildCapacity`), executed under the
pipeline mutex. Measured shapes (the close/reopen/rebuild captures exercise
exactly this re-stream; the per-cycle rebuilds exercise the same
`RebuildVolumeFromStore` primitives):

| quantity | 200k | 1M |
|---|---:|---:|
| full reload (open + re-stream + accelerator rebuild) | 1,290 ms | 6,601 ms |
| rebuild-only cycles (10x, per cycle) | 857-990 ms | 4,923-8,980 ms (see 8.2 noise note) |
| filename-accelerator reconstruction | included above (one `RebuildFilenameIndex` per reload) | included above |
| memory after reload | private 106.8 MB | private 474.5 MB |
| teardown after close | private 5.6 MB (full release) | private 6.4 MB (full release) |
| snapshot export (pooled, cold-ish) | 52-55 ms | 255-268 ms |
| snapshot serialize + publish | 10-12 + 14-18 ms | 13-17 + 23 ms |

Search behavior during reload is governed by the existing read plane: search
never reloads (cold volumes stream from SQLite; `memory-bounded-indexed-scale`
D1/D3 contract), so a query issued while a volume is cold answers from the
durable stream (cold latencies in `memory-cold-decision.md`) rather than
blocking on hydration -- though search capture is serialized behind a reload
via the pipeline mutex (lane D P1-2). The over-watermark reload behavior
(a volume that cannot survive enforcement re-evicts and returns false) is
described in `memory-bounded-indexed-scale` and pinned by reload tests; after
review lane D P1-2 it is now FAIL-FAST (the reload estimates fit against the
low watermark before streaming, so an oversized volume cannot consume an
unbounded under-mutex transient and then fail). The 500 MiB figure is a
POLICY WATERMARK, not a hard bound: the estimator discloses a residual
(allocator/compaction/reload transients briefly exceed it).
Repeated publication passes (2x pooled, 2x owning per scale) show no growth
in publish time or serialized bytes (payload hash identical across passes).

## 8.2 Repeated-cycle stability (10 cycles)

`rebuild_cycles[1..10]` per scale, each a full replacement rebuild through
`RebuildVolumeFromStore` (the production mass-removal + re-ingest path):

- **Rebuild revalidation (lane E E-4 correction):** 1M rebuild cycles measure
  4.9-9.0 s vs the pre-fix 158-164 s. The honest band is **28-32x typical**
  (6 of 10 cycles at 4.9-5.7 s), **>=17.7x worst observed** (one unattributed
  4-cycle transient to 6.5-9.0 s; no host telemetry was collected, so the
  cause is unverified -- memory was flat throughout). The historical 31-34x
  figure stands as measured on the after-fix tree's 3 cycles; at `b6df9ef`
  the steady-state summary is ~5-6 s with up to ~1.8x transient variance.
- **Memory flat (no monotonic growth):** 200k live requested 72.4 -> 73.2 MB
  then flat; private bytes oscillate 98.9-106.8 MB (allocator/capacity noise,
  not growth: cycle 10 private is within the cycle-2..10 band). 1M live
  requested 345.5 -> 349.5 MB then flat across cycles 2-10; private
  459.6-479.3 MB flat-band. Working-set split and peaks are in the JSON.
- **Allocations:** live allocation count grows +1 per cycle at both scales
  (200k: 403,039 -> 403,050; 1M: 2,003,064 -> 2,003,075) with ZERO byte
  growth -- deterministic one-node retention per cycle (consistent with
  per-cycle bench record bookkeeping; product-side requested bytes are flat).
  Classified: non-material, bench-side; flagged here rather than hidden.
- **Handles/threads/WAL:** captured per cycle in
  `memory-report-10cycles.json` (`resourcesPostQuery`-style samples); the
  close/reopen captures prove full teardown (private drops to ~6 MB both
  scales), so nothing accumulates across restarts.
- **Snapshots:** repeated publication passes keep exactly the current
  mapping (publisher retains one generation; `SnapshotPublisher.h` contract),
  payload-identical across passes; generation counter grows by design.

Verdict: **resource cycles are STABLE (warm plateau), not leak-free-by-few-
cycles**: 10 cycles per scale, byte-flat after cycle 2, full-teardown proof,
and the one open sub-item (+1 allocation/cycle) explicitly classified as
bench bookkeeping with zero byte growth.

## Rebuild revalidation (mission step 14)

The 31-34x repeated-rebuild fix (`Projection::RemoveBatch`) is RETAINED after
the review-remediation work (the fold-exceptional rule only changes a
predicate on the already-required name scan; the removal paths were not
touched). Numbers above are from the final tip binary (`b6df9ef`).
