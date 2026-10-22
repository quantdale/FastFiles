# Resource measurements (task 4.2; reviewer-E P1-1/P1-2 closure) — 2026-10-22

Harness: `tests/indexstore/bench_candidate_index_resources.cpp` (production
Store schema; maintenance A/B via `SetTestingCandidateMaintenanceDisabled`;
WAL sampled at 100 ms; production checkpoint policy `CheckpointIfWalExceeds(32 MiB)`
applied at batch boundaries — without it the WAL peak is a bench artifact,
not production). Artifacts: `resources-200k.json`, `resources-1m*.json`
(identity fields embedded in these artifacts per the reviewer-D/F lesson).

## Disk (P1-1)

| Measurement | 200k | 1M |
|---|---:|---:|
| entries-only DB | 12.9 MB | 64.9 MB |
| entries + candidate index | 37.8 MB | 188.9 MB |
| **index delta** | **25.0 MB** | **124.0 MB** |
| **per 1M rows** | 124.8 MB | **124.0 MB** |

The production-schema figure is **~124 MB per 1M rows** (FTS5 trigram shadow
tables + map + side-set). The prototype's "~170 MB/1M" (entries+fts probe
schema) and the "+120 MB/1M" shorthand are reconciled: the production delta
is 124 MB/1M. Cross-check at 2M (full pipeline bench): database 147.2 MB ->
387.7 MB = +240.5 MB (~120 MB/1M) — consistent.

## WAL (P1-2)

| Policy | 1M peak |
|---|---:|
| Uncheckpointed (bench artifact) | 376 MB |
| **Production policy (32 MiB threshold)** | **3.4 MB** |

WAL growth is bounded by the production checkpoint policy; the uncheckpointed
number is recorded to show why the policy matters. Per-batch WAL is bounded
by the <=1 MiB wire batch; migration/rebuild runs in per-volume transactions
(peak observed small at 1M; 2M/5M migration WAL remains a disclosed gap).

## Memory

Peak private bytes during ingestion: 8 MB (both arms) — the index is
disk-backed; no candidate materialization in RAM. Fold-probe caches are fixed
(~192 KiB total). HostFoldSignature is computed once per process (bounded,
memoized).

## Ingestion (§14 end-to-end guard)

| Level | Baseline | Candidate | Ratio |
|---|---:|---:|---:|
| Store-layer tight loop (1M) | 168-265k rows/s | 53-64k rows/s | ~2.6-5x |
| **End-to-end pipeline (2M durable insert)** | **103.8 s** | **154.4 s** | **1.49x** |

The pipeline has non-store stages (projection build, fixture IO), so the
end-to-end cost is **1.49x — below the "doubles every ingestion batch =>
reconsider" threshold** (mission §14). The accepted trade: ~49% slower scan
ingestion for 1123-1528x cold-query wins on the target classes.

## Migration / rebuild timings (1M)

- v2->v3 migration (drop-simulation + rebuild): **11.2 s**
- rebuild-on-open (fold-signature mismatch = host change): **23.7 s**
  (includes the one-time exhaustive host-fold probe in a cold process)
- 2M/5M migration timing: **DISCLOSED GAP** (task 3.3's measurement half;
  the mechanism is identical and per-volume-transaction bounded).

## Disposition

- reviewer-E P1-1 (production-schema breakdown): CLOSED at the aggregate
  level (124 MB/1M measured; per-shadow-table breakdown needs
  SQLITE_ENABLE_DBSTAT_VTAB — deliberately NOT added to the production
  sqlite3 target; recorded as a disclosed limitation).
- reviewer-E P1-2 (WAL peak): CLOSED (3.4 MB under the production policy;
  uncheckpointed artifact explained).
- task 4.2: partially evidenced (bounds proven at 200k/1M + e2e at 2M;
  5M and per-table breakdown remain open).
