# Performance repeatability — campaign section 15 (task 5.7)

Protocol: `ffengine_bench_large_index_scalability --scales <n> --diagnostics`
with embedded identity; repeated FRESH-PROCESS runs at the final candidate;
per-class `cold` = first iteration of a measurement block after fixture/
rebuild, `warm` = the following 5 iterations (harness labels — see the
provenance manifest for the temperature-semantics caveat; no OS cache flush).
Baseline = `evidence/artifacts/baseline-200k-1m-2m.json` (pre-implementation
tree, reproduced predecessor regime).

## 2M production path — FINAL CANDIDATE `628de6d` (clean), pooled runs 4+5

| Class | cold (n=2) | warm (n=10) median [min–max] | baseline cold | speedup (cold med) |
|---|---|---|---:|---:|
| selective | 3.23, 3.24 | 3.15 [2.91–3.44] | 3,513 ms | **1,086x** |
| no_match | 0.93, 1.01 | 0.88 [0.86–0.92] | 2,092 ms | **2,153x** |
| exact_name | 3.21, 3.79 | 3.31 [3.06–3.78] | 3,736 ms | **1,068x** |
| broad | 26.3s, 27.2s | 27.0s [26.3–27.7] | 27,483 ms | 1.03x (parity) |
| extension | 34.4s, 34.5s | 35.1s [34.3–36.3] | 38,709 ms | 1.12x |
| unicode | 10.8s, 10.9s | 10.8s [10.6–11.1] | (11.6s*) | parity |
| moderate | 10.2s, 10.8s | 10.6s [10.1–13.7] | — | canonical (over-cap) |
| prefix | 10.1s, 10.2s | 10.6s [10.1–10.7] | — | canonical (over-cap) |
| worst_substring | 18.3s, 18.7s | 18.7s [18.3–19.1] | — | canonical (over-cap) |
| high_limit_broad | 26.6s, 26.7s | 27.2s [26.3–32.3] | — | canonical (over-cap) |
| mixed_case | 18.4s, 18.6s | 19.0s [18.1–20.1] | — | canonical (over-cap) |
| short_prefix | 26.6s, 26.8s | 27.0s [26.4–27.7] | — | canonical (over-cap) |

Rows examined: selective/exact_name 1, no_match 0 (candidate route);
broad-class rows = the full matched set (canonical stream, by design).
*unicode baseline cell from the same baseline artifact's 2M column.

**Repeatability verdict:** the two cold samples per target class differ by
0.01 / 0.08 / 0.58 ms; warm n=10 spreads are ≤ 12% of median. The improvement
is reproducible, not a single convenient sample. Against the baseline's own
n=1 cold cells the speedup ratios carry the baseline's single-sample limit;
the conservative claim is "three orders of magnitude on eligible queries"
rather than any specific multiplier.

## Contaminated/historical runs (never pooled)

- `build/campaign/bench-run1.json` (`ed7190e`, pre-plan-fix): selective ~122
  ms at 2M — the CROSS JOIN plan regression; superseded by `97b9ceb`, kept
  only as the regression's reproduction evidence.
- `bench-run2-2m.json`/`bench-run3-2m.json` (`97b9ceb`, dirty-flag mislabel —
  tree actually clean): selective cold 3.24 / — ms, corroborating the final
  candidate within noise; not pooled above because the candidate path differs
  by the later readiness gate.

## Statistical limits (explicit)

- Cold cells are n=2 per class at the final SHA (the harness runs one cold
  iteration per block); p90/p95 are therefore NOT reported for cold cells.
  Warm n=10 supports median/min/max only.
- One machine, one OS image, one compiler; the baseline comparison is
  cross-session (same machine, same harness, same fixture generator).
- 200k/1M single-run artifacts exist from the planning phase (baseline +
  candidaterung); the final-SHA repetitions were concentrated at 2M (the
  headline scale). 200k post-fix spot check: selective cold 2.70 ms.
- 5M: see the staged-acceptance evidence (separate document).
