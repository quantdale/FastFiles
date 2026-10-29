All evidence gathered. Here is the report.

## Review — Lane C: 5M ingestion throughput collapse (READ-ONLY, worktree `durable-cold-search` @ b68767a)

### 1. Mined artifacts — windowed series (nothing rerun)

`build/campaign/bench-5m.log` lines 5–55 contain the decisive windowed series (`durable insert progress N/5,000,000 (t s)`, ~98,304-row windows = 12×8,192-row batches). Tabulated segments (window rate):

| Rows (cum) | Δt (s) | rows/s in window | µs/row |
|---|---:|---:|---:|
| 0–1.0M | 37.9 | 26,600 | 38 |
| 1.0–2.0M | 98.3 | 10,200 | 98 |
| 2.0–3.0M | 139.3 | 7,200 | 139 |
| 3.0–3.1M | 14.7 | 6,700 | 150 |
| **3.105–3.203M** | **93.5** | **1,050** | **950 ← CLIFF** |
| 3.2–3.3M | 170.2 | 578 | 1,730 |
| 3.3–3.5M | 383.8 | ~520 | ~1,900 |
| 3.5–3.7M | 419.3 | ~467 | ~2,100 |
| 3.70–3.91M | 177.3 | 1,155 (transient recovery) | 870 |
| 3.9–4.0M | 133.7 | 736 | 1,360 |
| 4.0–5.0M | 1,261 | 402 | 2,500 |
| last window (4.907–5M) | 413.4 | **227** | 4,400 |

Two regimes: **Phase 1 (0–3.1M)** per-row cost grows smoothly ~linearly in n (38→150 µs/row from 1M→3.1M) = O(n) per row ⇒ O(n²) total; **Phase 2 (≥3.15M)** a ~6–10× **step**, then continued growth to ~4.4 ms/row. The 2M repeats confirm reproducibility before the cliff: 148.5 s / 157.2 s / 153.2 s (`bench-run1.log:114`, `bench-run4-2m.json:11`, `bench-run5-2m.json:11`), all matching the 5M run's 136 s at 2M — so the collapse is scale-triggered, not run noise. `evidence/artifacts/candidaterung-200k-1m-2m.log:47-56` shows the same Phase-1 growth on a slower host (1M: 2.2→15 s/100k-window). `lifecycle-regression-matrix.md:16-24,41` gives fixed-arm scaling (rename-100k: 3.5–9.7× vs entries-only at 100k–2M table sizes) and store-layer A/B at 2M: 33.3 s (FTS on) vs 6.3 s (off). `resource-measurements.md` gives 124 MB/1M disk, 3.4 MB WAL under the production checkpoint policy (bench runs are uncheckpointed — 376 MB WAL artifact).

Note: `5m-staged-acceptance.md`'s "~1.8x the linear extrapolation from 2M" contradicts its own artifacts: 5,266 s vs 153×2.5≈383 s is **~13.7×** linear extrapolation (P2 documentation error).

### 2. Source analysis — candidate mechanisms (write path)

Per-row work in `Store::ApplyBatch` (`src/indexstore/src/Store.cpp:1327-1457`, one `BEGIN IMMEDIATE…COMMIT` per 8,192-row batch — `tests/engine/bench_large_index_scalability.cpp:45` `kBatchSize=8192`):

- entries upsert (`Store.cpp:1337`, WITHOUT ROWID `Store.cpp:52-69`): O(log n), sequential FRNs append at the tree edge. Not superlinear.
- FRN map: `mapUpsert` + separate `CandidateTermRowid` SELECT (`Store.cpp:462-466, 502-513`): two O(log n) seeks per row. Fine.
- FTS5 maintenance (`CandidateIndexUpsert`, `Store.cpp:526-577`): `termsUpdate`-miss-then-`termsInsert` (`Store.cpp:551-560`); prepared once per batch (`Store.cpp:471-496`) — reuse is correct, re-prepare is not a factor. FTS5 INSERT cost is O(nameLen) postings + amortized segment-merge; `entry_name_terms` uses full content storage (default, `Store.cpp:109-110`) so `%_content`/`%_docsize` add two shadow inserts/row. Expected O(log n)-ish amortized per row; measured growth (3.5→9.7×, 200k→1M) is **worse than log — hypothesis: FTS5 segment-merge/automerge churn from 8,192-doc transactions**, growing with segment count.
- Exception side-set (`Store.cpp:562-576`): every row executes `markException` (INSERT OR IGNORE) **and** `clearException` (DELETE) even though the fixture has zero fold-exceptional rows — 2 wasted O(log n) ops/row (P2 inefficiency, constant-factor only).

**Primary root-cause hypothesis (mechanism-verified, dominance unprofiled): periodic full-database `ANALYZE`.** `IndexPipeline::ApplyMftBatchChanges` calls `MaybeAnalyzeAfterBulkInsertLocked` after every batch (`src/engine/src/IndexPipeline.cpp:735`); the threshold is **10,000 records** (`src/engine/src/IndexPipeline.h:460` `kAnalyzeRecordThreshold`) and it resets via `recordsSinceAnalyze_ %= kAnalyzeRecordThreshold` (`IndexPipeline.cpp:554`) — with 8,192-row batches this runs a full `ANALYZE` (`Store::Analyze()`, `Store.cpp:1014-1016`) on **~6 of every 7 batches** (~one full scan of all entries + FTS shadow tables + map per ~9k rows). Total ANALYZE work is O(n²/batch): per-row cost ∝ DB size — exactly Phase 1's linear-in-n slope (38→150 µs/row at 1M→3.1M) and Phase 2's magnitude (~30 s/batch = one cached-scan→I/O-bound scan of a ~0.6–1 GB DB). The **step at ~3.1M** is consistent (hypothesis) with the ANALYZE working set (DB ~620 MB + uncheckpointed WAL ~250 MB + projection) crossing available cache headroom while the host ran certification builds (disclosed in `5m-staged-acceptance.md`). Secondary hypothesis: FTS5 merge I/O amplification over the same threshold. Checkpoint policy is **not** on the bench path (`CheckpointIfWalExceeds` only from `RunMaintenance`, `IndexPipeline.cpp:526-527`; the bench calls only `ApplyMftBatch`), so checkpoint interference is ruled out for this artifact.

### 3. Fixture vs ingestion cost

`generationMs` (5,266.35 s) spans only the apply loop: `generationStart` is taken after `pipeline.Open` ("fixture open 46.83 ms", `bench_large_index_scalability.cpp:565-567,578,625`). Record synthesis (`MakeMftRecord`/`FileName`) runs inline in the timed loop but is O(1) in-memory string work; there is no disk fixture. The 2M repeats (148.5/153.2/157.2 s) with identical synthesis bound it as a small constant. **The 5,266 s is pipeline ingestion (Store::ApplyBatch + FTS + ANALYZE triggers + projection upserts), not fixture generation.** Per-stage split inside that loop is not present in any artifact.

### 4. Minimal decisive profiling set (no SQLite config changes)

Existing timers partially suffice: `CandidateStatements.timings` (`Store.cpp:494-496`, `candidateMapMs/candidateTermsMs/candidateExceptionMs`) already splits map/terms/exception — but is wired only to `OpenPhaseTimings` (rebuild/migration, `Store.h:142-166`), nullptr in ApplyBatch. Missing accumulators: entries-upsert, `BEGIN`/`COMMIT`, and **wall-time around `store_.Analyze()`** (`IndexPipeline.cpp:553`). Experiments, per scale 200k/500k/1M/2M with the harness's existing 100k progress windows:

1. **Arm A** (entries-only via `SetTestingCandidateMaintenanceDisabled(true)`, `Store.cpp:1315`) and **Arm B** (current) through `ffengine_bench_large_index_scalability` — window curves already emitted; no new code. Runtime: B ≈ 5 s/50 s/2 min at 200k/1M/2M; A ≈ 3–5× cheaper. ~15 min total.
2. **Arm B−ANALYZE** (testing seam bypassing `MaybeAnalyzeAfterBulkInsertLocked`, same style as existing seams) — the decisive discriminator. If its window curve is flat while B grows linearly, ANALYZE is the growth term. ~10 min.
3. **Cliff probe: B to 3.2M with the 3 extra timers** (entries/commit/ANALYZE accumulators, reported per window) — identifies which stage steps at 3.105–3.203M. ~7–10 min (the cliff is at ~5 min into B).
4. Read-only counters per window, no config change: `sqlite3_db_status(SQLITE_DBSTATUS_CACHE_MISS/HIT)` and wall-ms of `Analyze()` — separates cache-miss collapse from algorithmic growth.

Total ≈ 45–60 min single-threaded, 2× ≈ 2 h for repeatability.

### 5. Optimization arms — what's worth running

- **A (entries-only)**: run as the measurement baseline only. REJECT as a product mode — cold queries regress to 2–38 s (`lifecycle-regression-matrix.md:53`); it exists to quantify the FTS delta.
- **B (current)**: RETAIN **if** after the ANALYZE fix (defer `Analyze()` to `RunMaintenance`/idle — effectively arm C's payload) the 5M curve is near-linear (≤ ~2.5–3× the 2M linear extrapolation, i.e., ≤12–15 min at 5M) and B−ANALYZE per-row growth 1M→2M is ≤2×. REJECT/defer B if `candidateTermsMs` slope alone remains superlinear past 2M.
- **C (batching)**: the ANALYZE deferral is the minimal C variant and is the highest-value change. Pure "bigger transactions" is constrained: production wire batches are ≤1 MiB (`kMaxFrameSize`) and 8,192 records ≈ that cap, so coalescing changes crash-recovery granularity (ApplyBatch retry contract, `Store.h:253-256`). RETAIN C only in the deferred-ANALYZE form unless timers show commit overhead >30% of batch time.
- **E (bulk-build + one `RebuildCandidateIndex`, `Store.cpp:629`)**: RETAIN as documented fallback (ADR already keeps durable-postings B as fallback) **only if** B−ANALYZE still shows superlinear FTS merge growth; measured rebuild costs (v2→v3 108–137 s at 5M per `tasks.md:22`; rebuild-on-open ≈35 min per acceptance doc) make E a one-time-cost trade, not a default.

**Verdict: OK with notes** (investigation only; no diff to gate). Highest-confidence finding is the verified mechanism of full-database ANALYZE every ~9k rows (`IndexPipeline.h:460`, `IndexPipeline.cpp:553`) as the root-cause candidate; confirm with experiment set §4 items 2–3 before choosing arms. P2: correct the "~1.8x linear extrapolation" arithmetic in `5m-staged-acceptance.md`.