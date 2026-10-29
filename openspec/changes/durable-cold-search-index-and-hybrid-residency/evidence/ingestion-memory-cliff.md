# 5M ingestion cliff — stage-isolated cause and bounded-memory correction

This supersedes the hypothesis in `ingestion-analyze-ab.md` that per-batch
ANALYZE explained the entire 5M collapse. ANALYZE was a genuine quadratic
cost, but did not cause the abrupt 3.1M-row cliff.

## Disproof and stage isolation

At clean `0dba633`, the ordinary benchmark's production ingestion fixture
(`--memory-report --scales 5000000 --rebuild-cycles 1`) still crossed the same
cliff: 3,104,768 rows at 82.51s, 3,203,072 at 162.11s, 3,604,480 at 885.65s.
The run was stopped at its 900-second safety limit; it did NOT complete 5M.

`ffengine_bench_ingestion_profile` reuses the EXACT fixture builder from
`bench_large_index_scalability.cpp` and compiles per-batch stage timing into
its own `IndexPipeline.cpp` only (`FF_INGESTION_PROFILE`). Shipping targets
contain none of these timers or logging. No SQLite configuration changes.

Representative 8,192-row batches at/after the cliff (pre-fallback):

| Stage | elapsed |
|---|---:|
| Store::ApplyBatch (entries + FRN map + FTS + side-set + commit) | 148–197ms |
| Projection upserts | 31–37ms |
| Rule/reconciliation bookkeeping | 0.15–0.20ms |
| **Projection memory enforcement** | **12,455–13,190ms** |

Eviction count rises by **eight per batch** while the estimate INCREASES
from 485,252,848 to 498,216,304 bytes. The eight-pass bound does not reach
the 400MiB low watermark (500MiB budget). Each pass evicts a tiny directory
and compacts the entire multi-million-row projection, then the next batch
repeats that work. This is the dominant measured cliff cost, not FTS insertion
(~0.17s versus ~12.8s enforcement).

Artifacts: `build/campaign/final-acceptance/ingestion-5m.log` (clean timed-out
run), `profile-5m.log` (bounded 240s diagnostic experiment).

## Narrow correction

`Projection::EnforceMemoryLimit` still tries the existing eight fine-grained
LRU subtree evictions. If they cannot reach the watermark, it evicts the LRU
victim's **whole volume** and marks it cold, repeating per volume only if
necessary. Each fallback removes an entire resident volume, not another tiny
subtree. Canonical durable search already supports cold volumes. Default
500MiB budget, durable entries, FTS maintenance and transactionality are
unchanged. The trade-off is coarser hot residency after fine-grained eviction
has failed; it is preferable to over-budget growth and repeated compactions.
Existing residency-epoch propagation remains in place.

Red-first regression `TestTinyVictimsCannotSustainAnOverBudgetProjection`:
20 tiny directories plus 10,000 files, 64KiB test budget. Before the fix,
exactly the budget-convergence assertion fails; after the fix it passes and
the volume is cold. Projection, pipeline and concurrency suites pass.

## First measured candidate result (diagnostic build, n=1)

The same 5M fixture completes in **136,261.964ms** (136.26s) with the
correction. Near the end: store 202–223ms/batch, projection 8–35ms,
enforcement ~0.001ms, final projection estimate **234,213,198 bytes**;
eviction count stays 20 instead of growing eight per batch. Thus the
previously observed cliff is eliminated in this controlled instrumented run.

This is not yet a clean final-candidate benchmark distribution. A normal
uninstrumented clean-build rerun, entries-only arm, repeated small-scale
benchmarks and independent review remain required. Historical 5,266s versus
136s is NOT advertised as a statistically controlled multiplier (different
host contention and earlier ANALYZE behavior). FTS-internal merge/cache/I/O
substage work is not separately profiled; this experiment isolates the
measured dominant cliff stage without attributing it to FTS.
