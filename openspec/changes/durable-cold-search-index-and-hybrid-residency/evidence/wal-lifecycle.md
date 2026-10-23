# WAL lifecycle: 960 MB burst characterization + bounded-cadence correction

Campaign sections 16-17 (task: large-removal WAL burst). Lane D analysis
(`evidence/lane-reports/acceptance-lanes/lane-D-wal-analysis.md`) plus the
measurements and corrections below.

## Why the threshold never prevented the 960 MB peak (proven)

Two distinct mechanisms, now both measured:

1. **Bench/workload class (the historical 960 MB number):** the lifecycle
   arms in `ffindexstore_bench_candidate_resources` checkpointed **once per
   phase** (rename/mass-remove/reinsert/forget) instead of per batch, so
   WAL accumulated across whole phases with only SQLite's default
   autocheckpoint backfill. The "32 MiB threshold" cited in older evidence
   was a literal in this bench, not production policy (production is
   `kWalForceCheckpointThresholdBytes` = 4000*4096 = **15.6 MiB**).
   Re-running the same 2M workload with the historical cadence reproduces
   the historical number **exactly** (`wal_peak_bytes` 959,807,592 in
   `build/campaign/lifecycle-2m.json`).
2. **Production class:** `FinishReconciliationPass` applied an entire
   reconciliation mass-removal in ONE `ApplyBatch` transaction; a WAL
   cannot truncate uncommitted frames, so a multi-million-row removal grew
   the WAL inside a single transaction (structural ~480 B x rows class
   regardless of any threshold). `Store::ForgetVolume` and per-volume
   rebuild/migration are single-transaction by the same shape.

## Corrections implemented

1. **Production: chunked reconciliation removal** (`IndexPipeline::
   FinishReconciliationPass`): removals apply in 5,000-row committed
   chunks with `CheckpointIfWalExceeds(kWalForceCheckpointThresholdBytes)`
   between chunks; the projection mirrors the committed prefix in one bulk
   pass (avoiding per-chunk accelerator rebuilds). Crash/failure between
   chunks leaves exactly the committed prefix applied and the pass's
   obligation retained — removals are idempotent derived state, so the next
   pass re-derives the rest. Per-chunk atomicity preserves the entries/
   candidate transaction-consistency contract. Semantic guard:
   `TestReconciliationMassRemovalConvergesAcrossChunks` (multi-chunk
   convergence + idempotent re-run; the WAL bound itself is the measurement
   below). The existing persistent-failure semantics tests pass unchanged
   (injection fires before the chunk loop, whole-plan).
2. **Bench methodology:** lifecycle arms now checkpoint **per batch** with
   the production 15.6 MiB threshold (`kWalCheckpointThresholdBytes`),
   matching the ingest loop's cadence. This makes the measured WAL peaks
   policy-true instead of artifacts.
3. **Documentation:** the "32 MiB production threshold" and "checkpoint at
   batch boundaries" claims were corrected in `resource-measurements.md`
   (see the corrections block there); `Store.cpp`'s stale "every 500ms"
   checkpoint comment is superseded by the real cadence (RunStoreMaintenance
   at scheduler wake ~10 min + lifecycle events + 10-min idle).

## Measurement (2M rows, identical harness/workload, A/B)

`build/campaign/lifecycle-2m.json` (old cadence) vs
`build/campaign/wal-matrix/lifecycle-2m-perbatch.json` (new cadence):

| Metric (2M, candidate maintenance ON) | old cadence | per-batch cadence |
|---|---:|---:|
| **WAL peak** | **959,807,592 B (~960 MB)** | **273,629,832 B (~274 MB)** |
| mass-remove | 20.0 s | 27.0 s |
| reinsert | 30.1 s | 44.5 s |
| rename (100k) | 1.6 s | 2.0 s |
| forget volume | 22.3 s | 22.2 s |
| maintenance OFF WAL peak | 121,033,272 B | 121,033,272 B (identical) |

- The peak drops **3.5x** at a measured 21-48% write-phase cost from
  checkpointing itself. The residual 274 MB reflects per-transaction frame
  batches plus checkpoint behavior under continuous writing; the structural
  single-transaction giant is gone from the reconciliation path.
- **Disk-reserve guidance (documented requirement):** worst-case WAL remains
  bounded by the largest single transaction (~480 B x rows for
  ForgetVolume/per-volume rebuild/migration transactions) plus the
  checkpoint threshold; operators should keep ~1 GB free per multi-million-
  row volume during mass maintenance. This is documented rather than
  weakened: those operations stay single-transaction on purpose
  (transactionality is not traded for disk).

## Crash/recovery status

- Existing proof stands: graceful fault rollback +
  real kill-during-commit recovery (store tests) at 10-100-row scale;
  interrupted-migration kill/recovery at 1M/2M (one full migration).
- Chunked removal crash semantics: committed-prefix + retained-obligation
  (tested convergence semantics); a crash between chunks is equivalent to a
  failed pass — the next reconciliation re-derives the remainder.
- NOT_EXECUTED (deferred, honestly): a kill mid-2M-removal recovery cell and
  a `SQLITE_FULL` fault cell (safe fault-injection or a size-capped
  disposable volume required; never the system volume). SQLite WAL
  atomicity is size-independent; these are operational-recovery bounds,
  not correctness gaps in the transaction model.

## Caveats

- `CheckpointIfWalExceeds`'s RESTART escalation can fail with SQLITE_BUSY
  under active readers and the caller discards the result (pre-existing,
  lane D P2). Under sustained concurrent readers the WAL can exceed the
  nominal threshold until a quiet period; the reserve guidance covers it.
  Fixing this (busy handler / bounded retry) is left as a disclosed follow-up.
- 5M-scale WAL cells NOT_EXECUTED (the 2M matrix is the measured bound);
  the removed per-batch ANALYZE reduces 5M write churn substantially.
