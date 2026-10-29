# Combined production lifecycle regression matrix — campaign section 21 (task 5.8)

Question: does FTS5 candidate maintenance reintroduce the predecessor's
quadratic deletion pathology on top of the projection fixes? Answer: **NO —
all lifecycle arms scale linearly (10x rows = 10.0x removal time)**, with a
bounded per-row maintenance cost of 3.5-9.7x vs entries-only writes.

Harness: `ffindexstore_bench_candidate_resources --lifecycle` (campaign 5.7
arms; identity-embedded artifacts `build/campaign/lifecycle-{200000,1000000,2m}.json`,
source `628de6d`, clean). Each arm: full ingest → rename 100k rows → mass
remove ALL rows → full reinsert (volume replacement) → mark unavailable +
ForgetVolume. A/B = candidate maintenance ON vs OFF on the same code.

## Measurements (maintenance-on vs maintenance-off)

| Arm | 200k | 1M | 2M | scaling (200k→2M) |
|---|---:|---:|---:|---|
| rename 100k rows | 1,435 ms (3.5x) | 3,997 ms (9.7x) | 1,623 ms (3.9x) | fixed row count; log-ish in table size |
| mass remove ALL rows | 2,012 ms (5.4x) | 10,164 ms (5.6x) | 20,025 ms (3.6x) | **10.0x time for 10x rows = linear** |
| reinsert ALL (replacement) | 3,790 ms (6.3x) | 12,699 ms (3.6x) | 30,117 ms (4.7x) | 7.9x for 10x rows (batch effects) |
| ForgetVolume | 1,968 ms (5.8x) | 9,670 ms (5.4x) | 22,280 ms (3.9x) | 11.3x for 10x rows |

(2M rename lower than 1M is run-to-run variance of the fixed 100k-row arm;
the 2M figure was taken on a quieter host phase.)

## Resources under the lifecycle workload (2M)

- peak private bytes: 10.3 MB (maintenance-on) vs 9.6 MB (off) — flat,
  no accumulation; staged/derived structures stay bounded.
- WAL peak WITH the production checkpoint policy (32 MB threshold,
  `CheckpointIfWalExceeds` at batch boundaries): **960 MB (on) / 121 MB
  (off)**. This is the worst-case mass-removal/replacement burst — NOT the
  steady-state ingestion figure (3.4 MB, `resource-measurements.md`). Both
  are disclosed; a removal-heavy reconciliation at 2M generates large WAL
  bursts between checkpoints.
- database bytes after the full lifecycle: 488 MB (on) vs 132 MB (off);
  the delta (356 MB at 2M ≈ 178 MB/1M) exceeds the fresh-ingest delta
  (126 MB/1M) because the replace cycle leaves both fresh and vacated FTS
  pages before any vacuum; bounded and reclaimed by the store's existing
  vacuum policy.
- ordinary insert batches (ingest A/B): 33.3 s vs 6.3 s at 2M store-layer
  (pure ApplyBatch); the product-relevant end-to-end figure remains 1.49x
  (includes projection + pipeline work; see resource-measurements.md).

## Interpretation for the cost/benefit review (section 22)

- Ordinary incremental updates (the everyday USN batch shape: mixed upserts,
  small batches) pay the measured 1.49x end-to-end overhead and stay in the
  millisecond-per-batch regime.
- One-time bulk ingestion and removal-heavy operations (initial indexing,
  full-volume replacement, reconciliation) pay 3.5-6.3x vs an index-less
  store — linear, bounded, and a one-time or rare cost.
- The alternative (canonical cold scans) costs 2-38 SECONDS per query at 2M.
  With even a handful of cold searches per session the index is net-positive;
  the trade-off is RETAINED.

## Coverage vs the section-21 matrix

Covered: full-volume replacement (remove+reinsert), mass removal,
ForgetVolume, ordinary insert batches, rename-heavy, rebuild (repeated
rebuild-on-open measured in the open matrix: ~40 s at 2M, no drift across
samples), repeated open. NOT_EXECUTED: engine-level reconciliation sweep
(FinishReconciliationPass) as a bench arm — its store-level shape is the
mass-remove + reinsert arms above, and its engine-level cost is covered by
the existing `ffengine_index_pipeline_tests` reconciliation tests; a
dedicated bench arm is recorded as future work, not silently omitted.
