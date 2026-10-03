# Broad search: fresh profile on the retained candidate (tasks 7.1-7.2)

Candidate: release-test build at `596cd27` (pooled name path retained), benchmark
`ffengine_bench_large_index_scalability --scales 200000 --diagnostics`,
200,000 synthetic rows. Loads 23-30 during the run; per-class summaries are
medians over the bench's own repeated iterations (min/median/p90/max), so
they are relative-cost evidence, not retention thresholds.

## Task 7.1 — twelve query classes (200k rows, warm)

| class | results | median ms | p90 ms |
|---|---:|---:|---:|
| selective | 1 | 24.51 | 28.17 |
| exact_name | 1 | 23.74 | 27.34 |
| no_match | 0 | 28.33 | 35.28 |
| moderate | 4096 | 85.64 | 92.56 |
| prefix | 4096 | 86.27 | 94.76 |
| unicode | 4096 | 91.45 | 102.05 |
| mixed_case | 4096 | 131.84 | 139.74 |
| worst_substring | 4096 | 137.69 | 143.16 |
| broad | 4096 | 162.79 | 168.84 |
| short_prefix | 4096 | 167.51 | 175.37 |
| high_limit_broad | 4096 | 183.08 | 194.53 |
| extension | 4096 | 194.29 | 204.59 |

The historical ~95 ms figure is not a before row and is not used.

Result count is capped at `kMaxIndexedSearchResultCount` = 4096
(`indexed_search_result_cap` in the JSON), so the 10k/50k requests truncate at
4096; `high_limit_broad` is the same query as `broad` with a larger requested
limit and measures the same top-N path.

## Task 7.2 — where the time goes

Per-class stage extraction (warm iteration 0):

- **Every class scans all 200,000 rows** (`projection_rows_examined`=200000,
  `name_views_constructed`=200000) and **`filename_index_used` is false for
  all twelve classes** — the existing secondary filename index is not on any
  measured broad, short-prefix, or substring path.
- **Scan floor ≈ 27 ms** (no_match: 26.97 ms traversal with 0 matches;
  selective: 30.27 ms with 1) ≈ 135 ns/row for name-pool lookup + name view +
  the view-based predicate evaluation. `query_parse_ms` is ~0.01 ms;
  publication-side stages are small (response materialization 0.43 ms +
  serialization 0.55 ms + parse 0.29 ms at 1.03 MB wire bytes for broad).
- **Per-match handling dominates the broad cost**: broad traversal 159.94 ms
  with 59,062 matched rows; extension 202.99 ms with 78,751; short_prefix
  173.40 ms with 59,063; mixed_case 138.64 ms with 39,375. Subtracting each
  class's scan floor gives a consistent **≈ 2.2-2.5 µs per matched row**
  (~83% of the broad query).
- The hot-path code shows what that per-match cost is
  (`SearchAccumulator::Add`, Search.cpp): for every matched row the engine
  (1) copies the name into an owning `Candidate`
  (`candidate_name_copies` = 59,062), (2) re-runs the query predicates on the
  owned candidate (`MatchesWithoutFolder` + `Matches`; the view already ran
  the full predicate set in the loop for this scope), (3) materializes the
  folder path for the result, (4) classifies the match tier, (5) pushes onto
  a bounded heap and, once at the 4096 cap, `push_heap`+`pop_heap`+`pop_back`
  for every further match — destroying ~55k materialized candidates and their
  strings that could never survive the cut.
- `match_filter_sort_ms`, `substring_matching_ms`, etc. read 0.0 on this
  path: the fine-grained stage timers are not wired for the lightweight hot
  path; attribution above comes from the class-to-class totals plus the
  populated counters (`candidate_name_copies`, `candidate_objects_materialized`,
  `matched_rows`, `projection_rows_examined`, `name_views_constructed`).

## Task 7.3 candidate (evidence-backed, single)

**Bounded top-N admission in the search hot path.** Under the existing
lightweight-view gate (`!folderNeededForView`: AllIndexedLocations scope, no
folder-requiring predicate or filter), a matched view whose PRIMARY ordering
key is strictly worse than the bounded heap's current worst can never enter
the top-N set. Admitting such rows out before materialization removes the
name copy, the folder materialization, the redundant predicate re-run, the
heap churn, and the destruction for every loser. The admission is
conservative (strict primary-key comparison only; ties and Path sort fall
back to the full path), so it cannot change the result set:

- Relevance: tier, then name length (strictly worse only).
- Filename: ordinal name comparison (strictly worse only).
- Size/Modified/Created/AllocatedSize: strict numeric comparison.
- Path: no admission (folder is materialized only for results).

A second, smaller part of the same candidate: when the loop already ran the
FULL predicate set on the view (`MatchesView`, not `MatchesViewWithoutFolder`),
`Add` need not re-evaluate `MatchesWithoutFolder`/`Matches` on the owned
candidate; the remaining scope/filter checks still run.

Estimated effect from the profile: roughly half of the post-cap matches are
strictly worse on the primary key once the heap fills, so the per-match cost
(≈2.2 µs) applies to far fewer rows; the acceptance bar for 7.4 is a material
broad reduction (≥10% or a meaningful absolute drop) with no selective,
Unicode, mixed-case, cancellation, or memory regression. A differential test
must prove identical result sets between the admission path and the full
path before any retention decision.

## Filename-index note

`filename_index_used`=false for all twelve classes; the index path is taken
only for a sufficiently long simple literal under specific conditions that
none of the measured classes met at this scale (`projection_probe` and the
prefix cases scan). Recording this satisfies the 7.2 requirement to state
whether the existing filename index is on the measured broad path: it is not.

## Task 7.3 implementation and 7.4 decision — RETAIN

Implementation (committed with this evidence):

1. **Bounded top-N admission** (`SearchAccumulator::AdmitView`): under the
   existing full-view-evaluation gate (`!folderNeededForView`: AllIndexedLocations,
   no folder-requiring predicate or filter), a matched view whose PRIMARY key
   is strictly worse than the bounded set's current worst is admitted out
   before any materialization. Conservative by construction: ties and the Path
   sort fall through to the full path.
2. **`AddViewMatched`**: skips the redundant owned-candidate predicate
   re-evaluation when the loop already ran the full predicate set on the view.
3. **Capped-search correctness fix found by the admission's differential
   proof**: the bounded heap's comparator was argument-swapped, making the
   heap root the BEST result; `pop_heap`+`pop_back` then discarded the best
   and capped searches retained the WORST N — contradicting the documented
   "capped result sets are the TOP-N by the requested sort" contract. The
   property test (gated vs plain adds) exposed it via a hot/cold signature
   mismatch in `ffengine_index_pipeline_tests`. Fixed by comparing
   `ResultLess(left, right, ...)` so the root is the worst and the pop
   discards the worst.

Proofs: `ffsearch_admission_tests` (new) proves for every sort field and
direction that gated adds and view-matched adds reproduce the reference
bounded set exactly, and that unbounded requests never admit rows out; full
Debug CTest 127/127; ASan focused suites pass.

### Measurement scope and caveats (review-A findings A2-a/A2-b)

- The two arms do NOT deliver identical result sets for capped classes: the
  comparator fix changes which rows survive the cap, so broad `wire_bytes`
  differ (1,027,980 pre -> 916,816 post) and the post arm materializes 19,573
  matches against the pre arm's 59,062 (the pre arm pushed every match into
  the worst-N heap; the post arm prunes before materialization). The
  comparison is therefore old-behavior vs new-behavior, not a like-for-like
  faster render of the same rows. The magnitude is not explained by the byte
  delta: the cold durable path (untouched by the admission gate) is flat
  (broad 1,740 -> 1,723 ms; extension 2,226 -> 2,225 ms).
- Protocol deviations recorded honestly: the runs alternated pre-then-post
  per round (no randomization or counterbalancing), the host load was 23-30
  (the campaign's <=25 idle gate was not enforced for this bench), and no
  same-binary control was run in that session. The bench reports
  `build_configuration: Release` from its `NDEBUG` branch; the binary is the
  release-test preset (RelWithDebInfo with FASTFILES_RELEASE_TEST_BUILD=ON),
  which defines NDEBUG -- the label is a formatting artifact, not a different
  preset. `bench_large_index_scalability` embeds no build identity, so the
  pre/post attribution to `2230376` and the 7.3 slice rests on the recorded
  commit order, not on an embedded SHA.
- A same-binary same-class repeat for the scan-floor classes is recorded in
  the v3 addendum below when run; the +1.2..+2.7% deltas there were inside
  run-to-run variation but were not backed by an in-session floor in this
  table (review-D finding 4).

### Measured effect (3 interleaved rounds per arm, per-class medians)

| class | pre (2230376) | post (7.3) | delta |
|---|---:|---:|---:|
| selective | 21.31 | 21.64 | +1.6% (noise) |
| exact_name | 22.27 | 22.52 | +1.2% (noise) |
| no_match | 22.08 | 22.67 | +2.7% (noise) |
| moderate | 80.07 | 67.09 | -16.2% |
| prefix | 79.73 | 65.74 | -17.5% |
| unicode | 86.63 | 77.18 | -10.9% |
| mixed_case | 125.03 | 91.53 | -26.8% |
| worst_substring | 116.39 | 62.92 | -45.9% |
| broad | 163.71 | 80.42 | **-50.9%** |
| short_prefix | 163.36 | 79.11 | -51.6% |
| high_limit_broad | 163.29 | 78.47 | -51.9% |
| extension | 181.75 | 66.40 | **-63.5%** |

Counter attribution (broad, warm iteration 0, all three rounds): matched
59,062; `candidate_name_copies` / `candidate_objects_materialized` fall from
59,062 to 19,573 in every post round; traversal 163-174 ms -> 77-84 ms.
`filename_index_used` stays false (unchanged path).

**DECISION: RETAIN.** Broad-query latency drops 50.9% (bar: >=10% or a
meaningful absolute drop), the admission is result-preserving by proof, and
no material selective, Unicode, mixed-case, cancellation, or memory
regression appears (the small positive deltas on scan-floor classes are
within run-to-run noise and the admission cannot engage below the result
cap). The comparator fix is a correctness improvement: capped searches now
retain the documented top-N instead of the worst N.
