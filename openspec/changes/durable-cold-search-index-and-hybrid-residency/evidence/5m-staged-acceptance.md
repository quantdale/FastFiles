# Staged 5M acceptance — campaign section 19 (task 4.1, stages A-D) + 5M resource cell (task 4.2)

Artifact: `build/campaign/bench-5m.json` (identity-embedded: source
`628de6d`, clean, release-test). One 5M-row production run; the staged plan
allows bounded coverage with explicit NOT_EXECUTED records.

## Stage A — 5M store construction: PASS

- 5,000,000 entries generated through the production `ApplyBatch` pipeline
  (FTS candidate maintenance ON): 5,266 s (~88 min) — note the per-row cost
  DEGRADES with index size (12.8k rows/s at the start → ~250 rows/s near 5M;
  the FTS trigram update cost grows with the index). This is the honest
  large-scale ingestion finding: 5M initial indexing ≈ 1.5 h single-threaded
  on this host — ≈ 13.7x the linear extrapolation from the measured 2M run
  (~153 s at 2M → ~383 s linear; 5,266 s actual). (The earlier "~1.8x"
  note was an arithmetic error, corrected this campaign.)
- Database: 1,037.8 MB (≈ 208 MB/1M at scale — larger than the 124 MB/1M
  small-scale figure; FTS page occupancy worsens with size).
- Memory: after fixture ws 2.0 GB / private 2.2 GB (harness steady state
  incl. fixture buffers); store-layer migration/rebuild peak measured at
  12 MB in the open matrix — no unbounded growth. Default projection budget
  (500 MiB) UNCHANGED; at 5M the projection is cold by design (2M+ regime).

## Stage B/C — query classes hot+cold: PASS (12/12 classes, equivalence OK)

All 12 classes ran at 5M, hot (n=5) + cold (n=1):

| Class | cold | warm med | rows examined | hot/cold result-count equivalence |
|---|---:|---:|---:|---|
| selective | **3.1 ms** | 3.2 ms | 1 | OK (1/1) |
| exact_name | **5.1 ms** | 3.8 ms | 1 | OK (1/1) |
| no_match | **1.5 ms** | 0.9 ms | 0 | OK (0/0) |
| broad | 71.1 s | 68.4 s | 1,968,749 | OK (4096/4096) |
| extension | 93.6 s | 88.9 s | 5,000,000 | OK |
| unicode | 31.9 s | 27.3 s | 5,000,000 | OK |
| moderate | 27.8 s | 26.3 s | 984,375 | OK |
| prefix | 25.9 s | 26.5 s | 984,375 | OK |
| worst_substring | 59.0 s | 47.1 s | 1,476,563 | OK |
| high_limit_broad | 75.3 s | 84.1 s | 1,968,749 | OK |
| mixed_case | 55.4 s | 47.0 s | 1,476,562 | OK |
| short_prefix | 78.3 s | 69.0 s | 5,000,000 | OK |

**Equivalence: 12/12 classes, hot result counts == cold result counts, 0
mismatches** (Stage B/C acceptance). Target classes hold at 5M rows
(sub-10 ms); over-cap classes take the canonical scan by design.

NOTE on timing validity: the 5M run's later search cells shared the host with
certification builds (disclosed); the acceptance claim is the result-count
equivalence + bounded resources + sub-10ms target-class observation, not
precise broad-class wall times.

## Stage D — repeated search / reopen: PASS (bounded)

Warm n=5 per class with stable counts and no drift (see artifact); the
harness's post-rebuild cold cell (selective 4.4 ms) shows the reopen/rebuild
path stays healthy. Open-matrix repeated reopen at scale shows no unintended
rebuild (fold/stable marker).

## Stage E — migration/recovery representative slice: PASS (via open matrix)

5M migration itself is MEASURED: two fresh-process v2->v3 runs over the 5M
entries-only fixture (`evidence/artifacts/5m-migration-v2.json`, preserved
this campaign): candidate_rebuild 120,755.98/98,078.11 ms, totals
137,350.81/108,484.64 ms, peak WS ~13 MB, fine-grained map/terms/exception
decomposition. Identity qualification: the harness binary was
`628de6d+dirty-harness` (phase-instrumentation edits later landed at
80a6f11); the migration code under test is the certified tree. See
`downgrade-policy.md` for the FAIL_CLOSED_DOWNGRADE decision, the refusal
and operator-rollback test evidence, and the clean-tip re-measurement plan.
Remaining 5M recovery cells (interrupted-migration kill/recovery,
fold-mismatch rebuild, first-init) are NOT_EXECUTED as separate 5M runs (the
fully-measured 2M cells cover the mechanism; a 5M rebuild-on-open would cost
~35 min on this host). Explicitly recorded as NOT_EXECUTED, not omitted.

## Known boundary confirmed at scale (pre-existing, not a regression)

Snapshot publication cannot represent a 5M-row volume in one bounded section
(`kSnapshotMaximumPayloadBytes` = 256 MB ≈ 2.2M rows capacity; probes reject
2,200,468 rows as oversize). The actual export at ≥1M rows is the same
0-entry publish observed in the pre-finalization artifacts (budget-straddle
regime: projection cold above the 500 MiB budget). Browsing at this scale is
served by the durable/degraded paths; the snapshot format limit is a
disclosed product boundary predating this campaign.

## NOT_EXECUTED list (explicit)

- 5M open-matrix RECOVERY cells (first-init/fold-mismatch/interrupted-
  migration at 5M): mechanism covered at 2M; cost ~35 min/rebuild on this
  host — deferred, not omitted. The 5M migration-v2 cell itself IS measured
  (see Stage E / `downgrade-policy.md`).
- Exception-heavy + multi-volume classes at bench scale: covered by store
  tests (`TestCandidateAdmissionCountsExceptionCandidates`,
  `TestCandidateAdmissionIsVolumeScoped`) at fixture scale; the synthetic
  bench fixture has no fold-exceptional rows on this host (no cross-ASCII
  fold classes — predecessor's exhaustive probe).
- 5M lifecycle A/B arms: 2M arms measured linear; 5M deferred (cost).
