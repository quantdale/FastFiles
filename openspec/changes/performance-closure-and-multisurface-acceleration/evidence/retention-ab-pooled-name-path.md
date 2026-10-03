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
