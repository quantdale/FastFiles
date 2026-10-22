# Architecture decision record — durable-cold-search-index-and-hybrid-residency

Decision date: 2026-10-22. Decision gate (mission §9) after task-group-1
measurements. All measurements on the successor worktree at `d3884ee`
(source-equivalent to certified `43fc3c1`), release-test, host: campaign
Windows 11 machine, single 1920x1080 display.

## Measurement evidence

| Source | Artifact |
|---|---|
| Fresh cold baseline (200k/1M/2M, 12 classes, hot+cold) | `build/worktrees/baseline-200k-1m-2m.json` (284 search runs; identity embedded) |
| Alternative A prototype (FTS5 trigram candidates, dual side-set arms) | `build/worktrees/fts5-probe-{200k,1m,2m}.json` + `tests/engine/bench_fts5_candidate_probe.cpp` |
| Alternatives B/D models (postings + sorted-key arms) | `build/worktrees/secondary-index-200k-1m-2m.json` (3 architectures x 3 scales) |
| Research lanes A-F | `evidence/lane-reports/lane-{A..F}.md` |

### Baseline reproduced (COLD_SEARCH_BASELINE_REPRODUCED)

2M cold: selective **3513 ms**, no_match **2092 ms**, exact_name **3736 ms**,
broad **27483 ms**, extension **38709 ms** (historical: ~2.5 s / ~27 s /
~37.6 s class — reproduced). 1M cold: selective 1145 ms (historical 1141 ms).
Latency source confirmed: full SQLite name-column stream
(`cold_rows_examined` = the prefilter survivor class or the whole volume;
`store_stream_ms` ≈ 99% of total). Harness-label caveat (lane F P1): runs
labelled "warm" at ≥2M are cold-stream under the default budget — recorded
and carried into Phase D's benchmark protocol.

## Decision

**SELECTED: Alternative A — SQLite FTS5 trigram candidate index**, in the
lane-A construction:

- `CREATE VIRTUAL TABLE entry_name_terms USING fts5(name_folded, frn
  UNINDEXED, volume_id UNINDEXED, tokenize="trigram case_sensitive 1")`,
  where `name_folded` is the ASCII-folded (FoldAscii) UTF-8 name.
- **Candidates = FTS hits ∪ sharp side-set** (host-derived fold-exceptional +
  surrogate names, fail-closed probe — the production
  `Projection::UnitMayFoldToAscii` rule). Canonical
  `CompareStringOrdinal(..., TRUE)` verification remains the sole authority.
- Maintenance **inside the existing `Store::ApplyBatch` transaction**
  (crash consistency for free: index and entries commit atomically).
- Additive schema v3 (`user_version`), rebuild-on-open fallback; a v2 app on
  a v3 DB fails closed (existing newer-version refusal, `Store.cpp:408-411`).

### Measured results (prototype, 2M rows)

| Class | Baseline cold | FTS candidate-gen (sharp arm) | Candidates | False negatives (both arms) |
|---|---:|---:|---:|---:|
| selective | 3513 ms | **46.4 ms** (~76x) | 1 | 0 |
| exact_name | 3736 ms | **67.4 ms** (~55x) | 1 | 0 |
| no_match | 2092 ms | **50.5 ms** (~41x) | 0 | 0 |
| moderate | 10966 ms | 366 ms | 200k | 0 |
| broad | 27483 ms | 884 ms | 600k | 0 |
| worst_substring | 19826 ms | 755 ms | 400k | 0 |

The **sub-200 ms target at 2M for selective/exact/no-match is met at the
prototype level** (46-67 ms candidate generation + negligible single-row
verification). Store-layer ingestion with FTS maintenance: 88-92k rows/s
(one-transaction entries+FTS+exceptions — above the production pipeline's
~40k rows/s ceiling; a controlled entries-only A/B is a Phase D acceptance
guard per mission §16). Footprint: ~170 MB per 1M rows (probe entries +
trigram FTS + side-set); the trigram structure is the majority.

### Why A over B (durable postings) — weighted criteria

| Criterion | A (FTS5) | B (packed postings) | Weight |
|---|---|---|---|
| Correctness (superset + canonical authority) | equal (0 FN measured) | equal (contract identical) | 1 |
| Crash consistency | **wins**: same-`ApplyBatch`-transaction SQLite maintenance; tested internals | custom chunked posting maintenance; lane-B P1s (FRN identity, fold_signature) are new code | 2 |
| Latency @2M target | **meets** (46-67 ms) | projected equal (model bench: same trigram intersection shape) | 2 |
| Disk footprint | ~170 MB/1M | **wins**: 35-70 MB/1M packed (model measured 69-71 MB/1M) | 1 |
| Schema/migration risk | **wins**: one additive virtual table + user_version=3 | same migration shape plus custom format versioning | 2 |
| Ingestion overhead | measured 88-92k rows/s combined | unmeasured custom maintenance (~17 trigram writes/row projected equal) | 1 |
| Recovery | **wins**: rebuild-on-open = SQL rebuild | custom rebuild + corruption checks | 1 |
| Complexity | **wins**: no custom packed-format code (mission §10: prefer simple) | custom varint chunk codec + compaction | 1 |

B is **VIABLE and retained as fallback**: if Phase D's controlled ingestion
A/B shows FTS maintenance breaking the §16 ingestion guards, B's packed
postings (with lane-B's P1 fixes) is the redesign path. B is NOT rejected on
correctness or latency.

## Alternatives: decisions with reasons

| Alt | Verdict | Evidence-based reason |
|---|---|---|
| A SQLite FTS5 | **SELECTED** | 0 false negatives x 3 scales x 2 side-set arms; 41-76x candidate-gen win on target classes at 2M; same-transaction crash consistency; migration = additive table. Requires enabling `SQLITE_ENABLE_FTS5` in the vendored build and fixing the `SQLITE_ENABLE_FTS3=0` define (which currently ENABLES FTS3/4 — lane A P2). |
| B durable trigram postings | **DEFERRED (fallback)** | Model bench: 69-71 MB/1M rows, build 766 ms/1M; leaner disk but custom crash-consistent maintenance and two design P1s (FRN row identity — dense slots repack; host-bound `fold_signature`). Revisit only if A fails Phase D guards. |
| C hybrid hot/cold residency | **DEFERRED** | Lane C: signature layer answers only the 3 target classes (already covered by A); wider coverage needs a RAM-resident folded-key layer (17.3M refs ≈ 138 MB/1M rows at 8 B/ref) — a bounded-RAM architectural change whose benefit A already delivers on disk. Not rejected; out of this campaign's scope. |
| D covering ordinal-key index | **REJECTED** | Prior measurement (`bench_secondary_filename_index` decision record): prefix ranges miss canonical infix matches — the product's literal search is infix (`rejected_prefix_reason`). Correctness rejection, not performance. |
| E adaptive/raised budget | **REJECTED (control arm)** | 2M hot needs ~840 MB-1 GiB projection budget; raising default consumption is forbidden this campaign and unnecessary given A. Arithmetic documented; G5 flag measurement kept as optional control. |

## Consequences (implementation scope, tasks 3.x)

1. Vendored build: enable `SQLITE_ENABLE_FTS5`, remove the
   `SQLITE_ENABLE_FTS3=0` mis-define (drops unintended FTS3/4).
2. Schema v3 additive: `entry_name_terms` + `entry_name_exceptions_sharp`
   (fold-exceptional + surrogate side-set) + a `filename_index_meta`
   consistency marker; `user_version=3`; migration + rebuild-on-open +
   downgrade refusal (fail closed).
3. `Store::ApplyBatch`/`ForgetVolume` maintain the FTS + side-set tables in
   the same transactions; `StoreReadSession` gains a candidate rung
   (eligible ASCII literals, folded ≥ 3) feeding canonical verification;
   cost-based fallback to the full stream for ineligible shapes (and
   empirically-better-than-scan broad shapes decided by measured verify
   cost, not the hot-era 16,384 cap).
4. Acceptance: lane-D differential suites (A-D) with
   `SetTestingFoldExceptionalUnit` red-first cases, crash matrix C1-C9
   (lane E), ingestion/rebuild regression guards (§16), resource bounds.

## Honest open items at decision time

- Controlled entries-only vs entries+FTS ingestion A/B (probe measured
  combined only) — Phase D guard.
- 5M baseline staged (mission §5 disclosure): 2M is the measured decision
  scale; 5M runs in Phase D's acceptance with the lane-F staged plan.
- FTS query cost for broad shapes (366-900 ms candidate-gen at 2M) must be
  cost-compared per class against the cold scan in the planner; the hot-era
  candidate cap does not transfer blindly to the cold path.
