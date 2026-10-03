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
