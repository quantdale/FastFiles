# Ingestion scaling: root cause, A/B/C measurement, and decision gate

Campaign sections 11-15 (task: 5M ingestion collapse). Baseline `3e081f5`
(arm B), candidate `13a35c4` + `898d954` (arm C). Harness:
`ffengine_bench_large_index_scalability`, release-test, identical flags
(`--scales 200000,1000000 --output ... --preset release-test`), sequential
runs on the same host.

## Root cause (proven, mechanism level)

**Full-database `ANALYZE` on ~6 of every 7 ingestion batches.**
`IndexPipeline::MaybeAnalyzeAfterBulkInsertLocked` gated `store_.Analyze()`
on a 10,000-record threshold but reset the counter with `%=`; with
8,192-row wire batches the counter crosses 10,000 on nearly every batch
(16384->6384->14576->4576->...). `ANALYZE` scans every table and shadow
table (entries + `entry_name_terms*` + map + exceptions), so sustained
ingestion did O(n) extra work per batch = **O(n^2) total** — matching the
historical windowed series (`build/campaign/bench-5m.log`): smooth per-row
growth 38->150 us/row over 0-3.1M, then a cliff at ~3.105M rows when the
ANALYZE working set outgrew cache (~2,500 us/row near 5M). The same
statistics churn had already forced the `97b9ceb` CROSS JOIN plan pin
(mid-ingest plan flips), i.e. the per-batch ANALYZE was both the ingestion
cost AND a query-plan instability source.

Secondary observations (from lane C analysis, confirmed in source):
prepared-statement reuse is correct (per-batch prepare, cached); the
exception side-set does 2 redundant ops/row on non-exceptional rows
(constant factor; left unchanged); fixture synthesis is O(1) and not the
5,266 s (`generationMs` spans the apply loop only).

## The exposure: two plan-fragile queries depended on those stats

Removing per-batch ANALYZE (arm C, `13a35c4`) immediately regressed the
eligible classes to a size-scaled floor (selective 0.65ms@200k -> 71ms@1M;
exact_name 74ms@1M) and made `GetFolderAggregate`'s recursive CTE hang for
thousands of seconds at 200k. Both were **plan flips toward O(volume rows)
drives** when `sqlite_stat1` is absent:

- Candidate stream: implicit `entries JOIN map` let SQLite drive from the
  volume's entries. Pinned with the CROSS JOIN driver shape (same rationale
  as the Stage-2 pre-count pin).
- Aggregate CTE: `EXPLAIN QUERY PLAN` evidence (controlled probe,
  `build/campaign/analyze-ab/cte-plan*.db` procedure): **no stats ->
  `SEARCH e USING PRIMARY KEY (volume_id=?)` in the recursive step
  (O(subtree x volume)); stats -> `SEARCH e USING INDEX idx_entries_parent`
  (linear)**. Both CTE sites now pin `INDEXED BY idx_entries_parent`:
  600 s+ -> 0.11 s at 200k rows in the controlled probe, identical
  with/without stats; bench aggregate 560.6 ms == stats-era 568.6 ms.

Note the general lesson (recorded for the owner): query latency on this
store must not depend on `sqlite_stat1` freshness, because statistics
refresh is now a maintenance-cadence event. Both pins are semantic no-ops
(same result sets; stream order is unspecified-but-stable with ordering
imposed at the search layer).

## A/B/C results (identical harness; warm medians unless noted)

Generation (production `ApplyBatch` ingestion incl. FTS maintenance):

| Scale | arm B (baseline) | arm C (candidate) | ratio |
|---|---:|---:|---:|
| 200k | 5.9 s | **3.0 s** | 0.51 |
| 1M | 80.9 s | **20.9 s** | **0.26 (3.9x faster)** |

1M query cells (arm B -> arm C): selective 3.42->**2.37 ms**, exact_name
2.98->**2.87 ms**, no_match 1.05->**0.81 ms**, moderate 7.81->4.55 s,
prefix 7.16->4.52 s, broad 22.36->12.38 s, extension 21.20->16.17 s,
worst_substring 10.88->8.40 s, mixed_case 10.09->8.71 s, unicode
5.74->4.76 s, short_prefix 14.49->12.37 s, high_limit_broad
14.13->12.36 s. **24/24 cells improved or at parity** (ratios 0.48-0.98
at 200k; 0.55-0.96 at 1M). Cold cells track the same shape (1M cold
selective 2.52 ms, exact_name 2.31 ms, no_match 0.87 ms).

Intermediate arm (deferral only, no pins) is retained in
`build/campaign/analyze-ab/arm-c.log` as the red evidence for the plan
dependency (eligible classes 71-74 ms at 1M; blocked in the aggregate
probe). Red-first unit coverage:
`TestBulkIngestionDefersAnalyzeToMaintenance` (failed exactly once against
the old behavior; `build/ab-red-run.log` / `build/ab-green-run.log`).

## Decision gate (section 15)

- Baseline SHA `3e081f5`; candidate `13a35c4` + `898d954`; fixture: the
  harness's deterministic synthetic names (10 patterns, 10% Unicode);
  repetitions: n=1 whole-run per arm (disclosed) + the historical
  repeatability cells (`bench-run4/5-2m.json` anchor arm-B-class
  behavior); before/after table above.
- Resource impact: no SQLite configuration changes; no schema change; no
  new memory. Statistics staleness is now bounded by maintenance cadence
  (scheduler wake ~10 min + lifecycle events + 10-min idle) instead of
  being per-batch fresh — with both pins, no measured query depends on it.
- Correctness: store/read-session/pipeline/concurrency suites green with
  the pins; result identities unchanged (plan pins only).
- **Verdict: RETAIN.** The candidate eliminates the quadratic ingestion
  term (3.9x at 1M; expected far larger at 5M where the old curve was
  13.7x linear) while every measured query class is faster or at parity.

## Open/deferred

- 5M ingestion re-run (the old 5,266 s / 88 min numbers are PRE-FIX and
  stay labeled as such until the final-sha 5M ladder run; expected
  ~linear, minutes-scale — final-ladder item).
- Lane C's constant-factor items (exception side-set double ops/row;
  statement caching nits) = UNPROVEN benefit, REJECTED for now (below
  measurement noise vs the eliminated term).
- Arm A (entries-only) remains a measurement baseline only (cold queries
  regress 2-38 s without the durable index); not a product mode.
