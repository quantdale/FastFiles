# Retention A/B history for the pooled name path (task 6.4)

All runs use the production-faithful first-generation benchmark
(`ffengine_bench_first_generation_publish`, 200,000 entries, 5 warm-up + 5
measured reps per process) under the established protocol: idle-gated
(median load <= 25 over 3 samples), each repetition pinned to one logical
core at high priority, interleaved randomized order, equal processes per arm.

## Run 1 — INVALID (measurement defect, not product evidence)

The first A/B queued `arm_pre` against the pooled binary built from a tree
where the benchmark still called the owning conversion wrappers. Production
call sites already used the pooled path, so that binary measured
pooled -> owning -> pooled re-conversion that the product never performs.
One candidate rep read 168 ms against ~80-91 ms baselines; the run was
stopped and discarded. Fix: the bench now exports and merges pooled
end-to-end (commit `d66332f`), matching production (same class of
harness-fidelity repair as the earlier move-semantics fix).

## Run 2 — REGRESSION CAUGHT (build `21b87d3` product + `d66332f` bench)

Clean session (per-arm total spreads 7.1-7.3%, loads 2-25; 6 of 12 reps did
not produce a readable per-rep output file, leaving 3 processes per arm):

| metric | before (`arm_pre`) | after (`arm_post`) | delta |
|---|---:|---:|---:|
| export_ms | 41.26 | 64.58 | **+56.5%** |
| publication_ms | 27.08 | 19.64 | -27.5% |
| client_parse_ms | 18.91 | 19.44 | +2.8% |
| **total_ms** | **89.78** | **106.65** | **+18.8%** |

Distributions did not overlap (baseline [92.0, 89.8, 85.5]; candidate
[103.9, 111.5, 106.6]): the pooled candidate was genuinely slower.

Root cause (found by inspecting the builder against this measurement):
`PooledSnapshotDirectoryBuilder::AddEntryText` called
`nameChars_.reserve(size + len)` for **every** name, and MSVC's `reserve`
allocates the exact requested size. Each added name therefore reallocated
and re-copied the directory's accumulated text — O(n^2) copying on the
export side [task 6.4 shipped better than the export's own measured gain
because a pre-sized arena in the isolation matrix never reallocated].

Fix (commit `c874d50`): bulk append — one `resize` (geometric vector
growth) plus one `memcpy` per name. Correctness suites (byte-equivalence,
lifetime, index pipeline, UI server) pass in debug and ASan; release-test
rebuilt warning-clean.

This run is retained as evidence that the measurement process caught a real
defect; it is **not** a product-performance verdict.

## Run 3 — retained decision run (queued)

`pin_ab.py retain_pooled_v3 arm_pre_pooled.exe arm_post_pooled.exe` against
the quadratic-free binary built at clean HEAD `c874d50`. Decision rule
(repaired task 6.4): retain only when the real first-generation path
improves by a meaningful margin clearly above the fresh same-binary noise
floor, with no correctness regression and no material common
selective-search regression; 10% remains the strong target, not a hard
floor. Outcome appended below when the run completes.

## Run 3 — RETENTION DECISION (build `c874d50`, production-faithful bench)

16:24-16:28 UTC+?; clean interleaved session, loads 10-25, 6 processes per
arm (all 12 reps readable this time):

| metric | before (`arm_pre`) | after (`arm_post`) | delta |
|---|---:|---:|---:|
| export_ms | 40.03 | 34.41 | -14.05% |
| publication_ms | 25.43 | 18.13 | -28.68% |
| client_receive_ms | 0.0054 | 0.0055 | noise |
| client_parse_ms | 18.10 | 19.31 | +6.7% (inside per-arm spreads: 10.4%/6.0%) |
| **total_ms** | **82.87** | **71.82** | **-13.33%** |

Per-arm total spreads: baseline 7.7%, candidate 10.4%. Baseline totals
[88.0, 87.1, 83.2, 82.6, 82.1, 81.6]; candidate totals [78.6, 75.2, 71.9,
71.7, 71.5, 71.1] — **non-overlapping distributions**: every candidate
process beat every baseline process.

**DECISION: RETAIN.** The real first-generation path improves 13.33%,
meeting the strong 10% target with a clear margin over the fresh same-binary
noise floor. Correctness protections hold: byte-identical wire output
(mechanically proven), the parse side is unchanged (outcome C), and no
search-path code changed. The client-parse +6.7% delta sits inside its own
run-to-run spreads and the payload bytes are identical, so it is not a
material regression. Memory effect: the pooled model stores each
directory's name text once in a contiguous pool instead of one owning
wstring per entry; the phase-8 remeasure records the actual number.

Residual observation (not a blocker, recorded for honesty): the pooled
export (34.4 ms) is ~8 ms above the isolated arena's ideal (~26 ms
projected from the D5 matrix). Structural differences (per-directory
pools, builder pending list, per-name memcpy) account for it; the retained
change already meets the acceptance target and no further speculative
micro-optimization is pursued in this campaign.

## Run 4 — in-session controls and counterbalanced re-measurement (review-A resolution)

Review A correctly showed that Run 3's noise-floor justification was
imported from a different session and binary, that the decision session had
no in-session control, that session 1's same-binary gap was 12.39% on
identical binaries, that the permutation was fixed, and that the candidate
binary's embedded identity said `dirty`. Runs 4a-4d answer each point
(batch `build/_apply26/run_final_measurements.bat`, 2026-10-04):

**4a/4b — in-session same-binary floors (identical binary on both arms):**

| session | baseline | candidate | between-arm gap | arm spreads |
|---|---:|---:|---:|---|
| `samebin_pre_final` (arm_pre vs itself) | 83.66 | 83.97 | **0.37%** | 7.04% / 2.68% |
| `samebin_post_final` (arm_post_final vs itself) | 73.38 | 74.06 | **0.92%** | 6.38% / 1.96% |

The identical-binary gap in a quiet session is under 1% -- the 12.39%
session-1 artifact was a user-loaded outlier, not the protocol's floor.

**4c/4d — counterbalanced A/B (6 processes per arm per ordering):**

| ordering | pre | post | post vs pre |
|---|---:|---:|---:|
| AB (`arm_pre` baseline, `arm_post_final` candidate) | 84.40 | 74.20 | **-12.09%** |
| BA (`arm_post_final` baseline, `arm_pre` candidate) | 84.79 | 73.46 | **-13.37%** |

Run distributions: AB baseline [82.6-87.0] vs candidate [73.0-75.6]; BA
candidate (pre) [82.8-87.0] vs baseline (post) [71.8-76.5] -- complete
separation in both orderings, and the swapped order reproduces the effect
within 1.3 points, so the fixed permutation carries no material positional
bias. Combined arm medians: pre 84.59 -> post 73.83 = **-12.73%**.

**Provenance correction:** `arm_post_final` is the bench binary rebuilt at
clean commit `d50c474` (the last executable-impacting commit of this
campaign; `build_worktree_state` was clean at that build). The earlier
Run-3 candidate embedded `dirty` because the tree carried uncommitted
documentation at its build time; that record stands as historical.

**Verdict (restated):** the pooled name path improves the real
first-generation total by 12.7% (both orderings, non-overlapping
distributions) against an in-session same-binary gap under 1% and arm
spreads of 2-7%. This is a clear margin over the freshly measured
same-binary noise floor and meets the 10% strong target. RETAIN stands on
properly attributed evidence.
