# Performance baseline and the reverted successor — apply session 1

Machine-readable records live beside this file:

- `performance/benchmark-baseline.json` — the last full baseline run
- `performance/raw/*.log` — one log per process repetition per bench
- `performance/before-after-literal-matcher.json`, `performance/before-after-first-generation.json`
- `performance/successor-candidate-and-revert.md` — the record of what was tried and reverted

## 4.2 The baseline

All runs on the certified non-debug `release-test` build
(`RelWithDebInfo`, `FASTFILES_RELEASE_TEST_BUILD=ON`). Each bench ran as five separate
processes, each bench doing its own internal warm-up and measured repetitions; the
reported figure is the median of the five run medians, with the p90 of those runs and
the max across all runs as the tail.

| Bench | Metric | Baseline | Tail (max) |
| --- | --- | --- | --- |
| `ffsearch_bench_literal_matcher` | `current_api` median, 1,000,000 names, per query case | 7.5–44.5 ms by case | — |
| `ffengine_bench_first_generation_publish` | TOTAL first-generation delivery, 200,000 entries | **102.9 ms** | 122.4 ms |
| ↳ export | 42.1 ms | | |
| ↳ publish+broadcast | 43.2 ms | | |
| ↳ pipe receive | 0.006 ms | | |
| ↳ client parse | 16.6 ms | | |
| `ffengine_bench_large_index_scalability`, 100,000 rows | `selective` / `moderate` / `broad` | 0.50 / 26.9 / 94.1 ms | — |
| `ffindexstore_bench_projection_memory` | bytes/entry at 5,000,000 entries | **329.2** | 329.2 |
| `ffengine_bench_secondary_filename_index`, 100,000 rows | `linear_canonical_scan` per case | 2.5–9.6 ms | — |

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

| Query case | before (ms) | after (ms) | delta |
| --- | --- | --- | --- |
| `short_2` | 27.106 | 21.459 | **+20.8 %** |
| `short_3` | 25.706 | 20.809 | **+19.1 %** |
| `single` | 23.993 | 21.960 | +8.5 % |
| `mixed_case_miss` | 26.081 | 23.867 | +8.5 % |
| `early` | 27.215 | 25.664 | +5.7 % |
| `late` | 30.853 | 29.527 | +4.3 % |
| `short` | 28.867 | 28.349 | +1.8 % |
| `common_prefix` | 33.198 | 33.069 | +0.4 % |
| `long` | 21.419 | 21.351 | +0.3 % |
| `medium` | 28.066 | 28.299 | −0.8 % |
| `failed` | 26.910 | 26.924 | −0.1 % |
| `too_long` | 7.775 | 8.212 | −5.6 % |
| `unicode` | 41.345 | 44.518 | −7.7 % |
| `mixed_case_hit` | 26.718 | 32.126 | **−20.2 %** |

The user-visible path moved the wrong way:

| First-generation metric | before (ms) | after (ms) | delta |
| --- | --- | --- | --- |
| export | 42.120 | 44.636 | −6.0 % |
| publish+broadcast | 43.183 | 45.126 | −4.5 % |
| client parse | ~16.6 | ~17.7 | ~−6 % |
| **TOTAL** | **102.903** | **106.703** | **−3.7 %** |

Two further processes on the candidate gave 120.7 ms and 117.9 ms, so the regression is
consistent, not noise. The secondary-index and large-index benches showed no consistent
win either (`linear_canonical_scan.no_match` −14.5 %, `broad` −11.5 %, large-index
`selective` −8.0 %).

## Decision: reverted

The candidate improved only an isolated microbenchmark on short ASCII queries while
regressing the measured user-visible metric by ~4–6 %, and it lost up to 20 % on a
mixed-case case. That is precisely the outcome the change's own spec rejects:

> A non-improving optimization is not retained for its own sake … the campaign SHALL
> revert that optimization and SHALL record the negative result.

and design decision D6:

> An 8 % improvement on a real hot path may stay. A large microbenchmark win on an
> irrelevant path does not.

The edit was reverted. `src/search/src/Query.cpp` and `src/engine/src/IndexPipeline.cpp`
are back at their committed bytes; the deterministic test added for it was reverted with
it, because a reverted edit has nothing left to guard. The repo rebuilt clean, the
affected search and pipeline suites passed, and `check_doc_drift.ps1` passed.

## 5.5 Termination condition

No further locally executable successor is proven. Every measured number in this change is
in the records above; no optimization was retained without a same-method improvement.

**Termination condition: remaining ideas speculative or harmful.** The measured,
locally executable candidates were exhausted by the one that regressed. The plausible
next things — changing the projection's bytes/entry of 329.2 (the largest single number
found), or optimizing the ~17 ms client snapshot parse — would each be a speculative edit
in the sense the spec forbids: neither has a same-method before/after showing the
targeted metric is the constraint, and the projection figure is a stable design
characteristic, not a regression. The first-generation path is dominated by export and
publication, both of which are proportional to the 3,126-directory working set and are
already structurally bounded, so there is no measured hotspot to attack.

External boundaries stay external: the raw-volume completion bound, production signing,
clean-host install, hosted CI, and the interactive-foreground UIA and DPI/High-Contrast
matrices are unchanged by this change.
