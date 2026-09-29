# Performance baseline and the reverted successor — apply session 1

Machine-readable records live beside this file:

- `performance/baseline-shipped-tree.json` - the baseline measured from the clean
  shipped tree, with `source_sha` provenance. **Written by the review session.**
- `performance/apply-session-before-run.json` - the apply session's pre-edit run,
  which it left uncommitted in the ignored build tree and which the review session committed
  as evidence
- `performance/apply-session-after-run.json` - the apply session's post-edit run, the same
  record it had committed under the name `benchmark-baseline.json`
- `performance/apply-session-first-generation-run.json` - the apply session's user-visible
  metric run, including its own `build_identity` field
- `performance/before-after-literal-matcher.json`,
  `performance/before-after-first-generation.json` - the machine-readable before/after rows
- `performance/candidate-run-mislabeled-as-baseline.json` - the apply session's original
  `benchmark-baseline.json`, renamed by the review because it is the *candidate* run rather
  than a baseline, and preserved rather than deleted
- `performance/raw/*.log` - one log per process repetition per bench. The literal-matcher,
  first-generation and projection logs, plus the secondary-index logs, are the review's
  clean idle-host re-measurement; the large-index logs remain the apply session's
- `performance/successor-candidate-and-revert.md` - the record of what was tried and reverted

> **Review correction (material).** The apply session committed the successor's
> *candidate* measurement as `benchmark-baseline.json` and presented its numbers as
> the baseline, with no source SHA in the file. The repo's own workstate records that
> the apply-session worktree was dirty at that point (`PROVENANCE_MISMATCH`
> `DIRTY_BINARY_BUILD` was raised), so those rows were taken with the edit applied.
> `ffengine_bench_first_generation_publish` embeds the SHA in its own header line and
> stamps `e2a573c284cdbb3399cf99cf1d1d9cec9ee17748` on every repetition, which is how
> the mislabel was found. This file's prose below is the apply session's original
> record; it is kept as history and the numbers are not silently changed.

## 4.2 The baseline

All runs on the certified non-debug `release-test` build
(`RelWithDebInfo`, `FASTFILES_RELEASE_TEST_BUILD=ON`). Each bench ran as five separate
processes, each bench doing its own internal warm-up and measured repetitions; the
reported figure is the median of the five run medians, with the p90 of those runs and
the max across all runs as the tail.

| Bench | Metric | Baseline | Tail (max) |
| --- | --- | --- | --- |
| `ffsearch_bench_literal_matcher` | `current_api` median, 1,000,000 names, per query case | 7.5–47.2 ms by case | — |
| `ffengine_bench_first_generation_publish` | TOTAL first-generation delivery, 200,000 entries | **127.679 ms** | 149.581 ms |
| ↳ export | | 55.658 ms | |
| ↳ publish+broadcast | | 54.879 ms | |
| ↳ pipe receive | | 0.007 ms | |
| ↳ client parse | | 18.990 ms | |
| `ffengine_bench_large_index_scalability`, 100,000 rows | `selective` / `moderate` / `broad` | 0.54 / 27.66 / 94.81 ms | — |
| `ffindexstore_bench_projection_memory` | bytes/entry at 5,000,000 entries | **329.2** | 329.2 |
| `ffengine_bench_secondary_filename_index`, 100,000 rows | `linear_canonical_scan` per case | see `raw/` | — |

**Review correction (material).** This table's `first-generation delivery` row used to read
**102.9 ms** and was wrong. That figure was the *candidate* run's own median, and the apply
session further committed it under the name `benchmark-baseline.json`, so the repository
carried the successor's regressed number as its baseline. The 127.679 ms above is measured
from the clean shipped tree at implementation SHA `e2a573c` on an idle host in the post-apply
review session, and the bench binary stamps that SHA in its own output header, so the
provenance is self-evidencing. The two slow benches remain measured on the apply session's
host state; their rows are recorded as-is in `raw/` and are not re-measured.

The practical consequence is that the apply session's and the review's numbers are **not**
comparable: 127.679 ms exceeds both apply-session values because the two runs were taken under
different machine load. They are recorded side by side rather than blended, exactly as the
change's spec requires for same-method claims. Any future successor must compare against the
127.679 ms clean baseline on an idle host.

`ffengine_bench_first_generation_publish` is the harness added for task 4.3. It measures
the wait between the engine holding a large index and a real subscribed UI client holding
a parsed, usable generation of it: the bounded indexed-directory export, the
`UiServer` publication and broadcast over a real named pipe, and the client-side snapshot
parse. 3,126 directories and 199,999 entries were published per repetition.

## 4.4 The successor that was selected

From the baseline, the highest-value *locally executable* candidate was the literal
matcher's fold scan. `CandidateView::nameAsciiLower` existed on the view struct and the
projection already computed and stored a matching per-entry value
(`src/indexstore/src/Projection.cpp:460,481`), but `IndexPipeline`'s zero-copy capture
threw it away: `LightweightHotRow` carried only `nameIsAscii`, and the view was
constructed with `view.nameAsciiLower = false` (`src/engine/src/IndexPipeline.cpp:1671`).
So every ASCII name — including the all-lowercase class that dominates real volumes — paid
a per-character `FoldAscii16Char` on both the anchor and the window comparison, even
though folding is the identity for that class.

The candidate was to consume the flag and add fold-free comparison paths.

## 5.3 What was measured, and the decision

Same method, same machine, same `release-test` build, same fixtures, five measured
processes each. `current_api` is the production view path; `bmh` is the bench's own
reference Boyer-Moore-Horspool.

| Query case | before (ms) | after (ms) | clean baseline (ms) |
| --- | ---: | ---: | ---: |
| `short_2` | 27.106 | 21.459 | 27.942 |
| `short_3` | 25.706 | 20.809 | 27.026 |
| `single` | 23.993 | 21.960 | 25.853 |
| `mixed_case_miss` | 26.081 | 23.867 | 26.752 |
| `early` | 27.215 | 25.664 | 28.031 |
| `late` | 30.853 | 29.527 | 31.682 |
| `short` | 28.867 | 28.349 | 31.582 |
| `common_prefix` | 33.198 | 33.069 | 34.836 |
| `long` | 21.419 | 21.351 | 21.962 |
| `medium` | 28.066 | 28.299 | 29.698 |
| `failed` | 26.910 | 26.924 | 27.306 |
| `too_long` | 7.775 | 8.212 | 7.908 |
| `unicode` | 41.345 | 44.518 | 47.189 |
| `mixed_case_hit` | 26.718 | 32.126 | 28.746 |

**Provenance, corrected.** The post-apply review first marked this `before` column as unbacked and
was wrong: the apply session's pre-edit run was left in the ignored build tree as
`build/_bench_baseline_pre.json` and has now been committed as
`evidence/performance/apply-session-before-run.json`. All 14 of its rows match this `before`
column. Its sister file, `apply-session-after-run.json`, backs the `after` column, and is the very
run the apply session had committed under the misleading name `benchmark-baseline.json`.

What the apply session actually got wrong was narrower and still real: it committed only the
**after** run, named it as if it were a baseline, and left no source SHA in it. The review's
re-measurement of the clean shipped tree supplies a properly provenanced reference, and it agrees
closely with the apply session's own `before` column - `short_2` 27.942 against 27.106, `short_3`
27.026 against 25.706, `mixed_case_hit` 28.746 against 26.718 - which confirms that the `before`
column really did describe unmodified code.

The candidate genuinely improved the isolated matcher bench: `short_2` 27.106 to 21.459 ms, about
20.8 percent. It was refused anyway, on the only same-session comparison the apply session made,
and because `mixed_case_hit` and `unicode` lost time.

The user-visible path moved the wrong way **on the apply session's own same-session comparison**:

| First-generation metric | before (ms) | after (ms) | delta |
| --- | ---: | ---: | ---: |
| export | 42.120 | 44.636 | -6.0 % |
| publish+broadcast | 43.183 | 45.126 | -4.5 % |
| client parse | ~16.6 | ~17.7 | ~-6 % |
| **TOTAL** | **102.903** | **106.703** | **-3.7 %** |

**Review correction (material).** The rows above are the only same-session comparison in the
change. Its first-generation run carries `b449d95352460218820a79eb38486eed0c0592eb` in its own
`build_identity` field rather than the implementation SHA, and its raw logs span 104.103 to
148.927 ms, so a 3.7 percent difference is inside that run's own spread. The comparison is retained
because it is the reason the candidate was refused, but it must not be read as a statistically
proven regression. The clean, correctly provenanced figure is the 127.679 ms TOTAL above.

Two further processes on the candidate gave 120.7 ms and 117.9 ms. The secondary-index
and large-index benches showed no consistent win either (`linear_canonical_scan.no_match`
-14.5 %, `broad` -11.5 %, large-index `selective` -8.0 %).

**Review correction (material).** The apply session described the above as the regression being
"consistent, not noise". That claim is not supported: the five raw logs it persisted for this
metric span 104.103 to 148.927 ms, a spread far wider than the 3.7 percent difference it rested
on. The claim is withdrawn here. The candidate is still correctly reverted, but for the honest
reason that **no improvement to the user-visible metric was demonstrated at significance**, which
the spec independently requires.

## Decision: reverted

The candidate improved an isolated microbenchmark on short ASCII queries and moved the
user-visible metric the wrong way on the apply session's only same-session comparison, with a
recorded `mixed_case_hit` regression. That is precisely the outcome the change's own spec rejects:

> A non-improving optimization is not retained for its own sake … the campaign SHALL
> revert that optimization and SHALL record the negative result.

and design decision D6:

> An 8 % improvement on a real hot path may stay. A large microbenchmark win on an
> irrelevant path does not.

**Review correction (material).** The paragraph above previously asserted a "~4-6 %" regression of
the user-visible metric. That assertion came from the same noise-bounded comparison and is
withdrawn: the run's own raw logs span 104.103 to 148.927 ms, far wider than the claimed
difference. The stronger and defensible statement is the one the spec actually supports: the
candidate was reverted because **no improvement to the user-visible metric was demonstrated at
significance**, not because a 4-6 percent regression was proven. The revert decision itself
stands..

The edit was reverted. `src/search/src/Query.cpp` and `src/engine/src/IndexPipeline.cpp`
are back at their committed bytes; the deterministic test added for it was reverted with
it, because a reverted edit has nothing left to guard. The repo rebuilt clean, the
affected search and pipeline suites passed, and `check_doc_drift.ps1` passed.

## 5.5 Termination condition

No further locally executable successor is proven. Every measured number in this change is
in the records beside this file; no optimization was retained without a same-method improvement.

**Termination condition: remaining ideas speculative or harmful.** The measured,
locally executable candidates were exhausted by the one that was refused. The plausible
next things — changing the projection's bytes/entry of 329.2, or optimizing the client snapshot
parse — would each be a speculative edit in the sense the spec forbids: neither has a
same-method before/after on the clean baseline showing the targeted metric is the constraint, and
the projection figure is a stable design characteristic, not a regression.

**Review correction (material).** Two numbers this section previously cited as the problem are
now much larger than reported. The clean shipped-tree baseline is **127.679 ms TOTAL** for
first-generation delivery, not the 102.9 ms this section assumed, and the parse stage is
**18.990 ms**, not ~17 ms. Export (55.658 ms) and publication (54.879 ms) together dominate that
total, so the next successor with a real user-visible payoff would target the export path, and it
must now be measured against 127.679 ms on an idle host rather than against the historical
figures. It is still correctly refused here, because a same-method comparison has not been run.

External boundaries stay external: the raw-volume completion bound, production signing,
clean-host install, hosted CI, and the interactive-foreground UIA and DPI/High-Contrast
matrices are unchanged by this change.
