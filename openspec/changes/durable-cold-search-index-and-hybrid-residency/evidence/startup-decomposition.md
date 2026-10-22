# Startup decomposition & host-fold signature disposition — PR #15 finalization (campaign 5.1/5.2)

All numbers: release-test, `ffindexstore_bench_store_open`, fresh processes
(child-per-sample), identity-embedded artifacts in `build/campaign/open-matrix/`
(21 cells; source `1c427d7`-era dirty tree — the open path is byte-identical to
the final candidate's except the later lane-A-2 PRAGMA de-duplication, which
removes one duplicate execution and cannot increase any phase).

## Decomposition (valid-v3 reopen, cold fresh process, p50)

| Phase | 200k | 1M | 2M |
|---|---:|---:|---:|
| connection open | 0.3 ms | 0.5 ms | ~1 ms |
| **PRAGMA integrity_check** | **726 ms** | **3,722 ms** | **7,839 ms** |
| PRAGMA config | 0.006 ms | 0.006 ms | ~0.01 ms |
| schema init | 0.15 ms | 0.14 ms | ~0.2 ms |
| schema-version read | 0.008 ms | 0.008 ms | ~0.01 ms |
| **host-fold signature** | **42 ms** | **41 ms** | **42 ms** |
| signature compare | ~0 | ~0 | ~0 |
| consistency check (COUNT cross-check) | 4.5 ms | 21 ms | 44 ms |
| candidate rebuild | 0 | 0 | 0 |
| **total** | **773 ms** | **3,789 ms** | **7,939 ms** |

Warm same-process reopens: fold signature 0.0006 ms (memoized), integrity
check unchanged (~178-190 ms at 20k; the check is per-open, cache-warm does
not skip it).

## Findings (P1 disposition)

1. **The host-fold signature is NOT the startup problem.** It is ~41-42 ms
   CONSTANT per fresh process (max observed 61 ms; identical at every row
   count — row-count-independent), i.e. 5.4% of the 200k open and 0.5% of the
   2M open. Per campaign section 8 ("if the signature cost is negligible,
   document the finding and move on"), NO caching infrastructure is added;
   the in-process memoization already present is sufficient. Host-change
   invalidation (stored `fold_signature` vs host) is preserved verbatim and
   re-verified by lane A ("cross-host fold-completeness mechanism is intact
   and fail-closed").
2. **The dominant per-open cost is the full `PRAGMA integrity_check`** (99%+
   of valid-reopen time; ~600 MB of page reads per M rows; CPU-bound,
   wall≈CPU). It is the `index-durability-false-success-closure` D6/task-1.7
   durability gate (Corrupt vs Inconclusive classification before trusting
   disk). Reducing it (e.g. `quick_check`, bounded `integrity_check(N)`,
   amortization) WEAKENS corruption detection and is therefore out of scope
   for this campaign ("correctness before speed"; "do not redesign"). It is
   recorded here as the measured startup cost with a recommendation for a
   separate, spec-reviewed change if startup latency becomes a product
   requirement.
3. The historical "23.7 s rebuild-on-open at 1M" figure is a REBUILD cost
   (fold-mismatch/corrupt-marker recovery), not a reopen cost: this matrix
   measures valid-v3 reopen at 1M = 3.8 s, forced rebuild at 1M ≈ 22.6 s
   (p50 total incl. integrity on the larger indexed file). The original
   figure conflated the two; no reopen takes 23 s.

## Migration / recovery cells (fresh process per sample)

| Case (2M rows) | p50 total | decomposition |
|---|---:|---|
| v2→v3 migration (first v3 init) | 23.5 s | integrity 1.8 s + rebuild 20.1 s (map 4.9 + terms 12.5 + side-set 1.2 + fold 55 ms) + version bump |
| fold-signature mismatch (host change) | 49.1 s | integrity 8.5 s + rebuild 38.3 s (delete+rebuild of existing structures) |
| corrupt marker (state=0) | 52.9 s | same shape as fold-mismatch |
| interrupted migration (REAL ExitProcess mid-rebuild) + reopen | 23.4 s | recovery costs exactly one full migration; WAL rollback of the partial transaction verified |
| read-only file, migration required | FAILS CLOSED | open returns false, integrity_failed=false (D6-correct: not corrupt, unwritable) |
| read-only file, valid v3 | opens | SQLite read-only degradation; no write needed (measured; disclosed) |
| empty database (first-ever init) | 2.1 ms | trivial |

Peak working set during 2M migration: 12.1 MB (bounded). I/O at 2M
migration: 1.7 GB read / 0.8 GB write.

## Statistical limits

p50/p90/max are reported only where the cell has enough samples (n = 5-20
processes per cell; n = 3 for the interrupted/read-only cells). All samples
are fresh processes on one machine (Ryzen 5 7535HS, 24 GB RAM, MSVC 19.44,
RelWithDebInfo); the fold-signature constancy claim is row-count-independent
but single-machine (lane A-3b disposition: the cache decision is insensitive
to a 2x machine factor).
