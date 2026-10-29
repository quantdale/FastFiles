# Evidence: large-index-lifecycle-and-search-scalability

All measurements: release-test (RelWithDebInfo) on one idle Windows host; wall-clock
single-run arms where noted; machine metadata embedded in each JSON. Comparisons are
strongest within one report/harness. Identity nuance (recorded, not hidden): the
before artifacts embed `4415926`/`54feddd` (clean/dirty build labels) and the
after-fix artifacts embed `54feddd` with `--source-sha 2e0d819` — the binaries were
built from the working tree that became commit `2e0d819` (product tree identical);
final certification regenerates artifacts at a clean embedded identity.

## rebuild/

- `memory-report-before.json` — reproduction on current `main` product tree
  (`ffengine_bench_large_index_scalability --memory-report`, 200k+1M, 4 cycles).
  1M: first build 4,095.8 ms vs replacement cycles 158,877.3 / 158,252.3 /
  164,348.8 ms. 200k: 2,444.8 vs 3,856-5,085 ms.
- `rebuild-isolation-before.json` — differential isolation matrix (arms A-G,
  `FF_INDEXSTORE_TESTING` seam). 200k clean attribution: B1 production removal
  10,225.2 ms vs E1 removal without filename-accelerator maintenance 28.7 ms
  (99.7% of removal cost is per-entry trigram posting erase); G1 no-trigram
  residual 55.1 ms. Self-explaining fields (index validity, eviction state, rebuild
  verdict) — the 1M arms record the default-budget straddle (modeled 413.8MB >
  400MB watermark: rebuilds end evicted with the accelerator invalidated).
- `memory-report-after-fix.json` — same harness/command after the bulk-removal fix.
  1M cycles 4,715.7 / 5,100.8 / 4,772.3 ms (**31-34x**, at first-build cost
  4,686.9 ms); 200k 823-872 ms (4.4-5.8x). Memory unchanged (private ~461-477MB;
  live requested 345.9/349.9MB).
- `rebuild-isolation-after-fix.json` — arm-level: B1 22.0 ms at 200k (**465x**);
  all build phases at the fresh-build floor.
- `../search/search-matrix-after.json` — **post-rebuild-fix control** (same harness
  as the search baseline). Result sets are EXACTLY equal to the baseline for all
  24 class x scale pairs (the rebuild fix changes no search semantics). Honest
  attribution note: this run used a release-test binary built before the planner
  fix (only Debug carried it at that point), so its `filename_index_used=false`
  rows are NOT evidence about the planner fix — they are the stale-binary control.
  The planner-fix matrix is a separate run (`search-matrix-planner.json`).

## search/

- `search-matrix-before.json` — 12 query classes x 200k/1M/2M, hot + cold,
  `--diagnostics`. Hot p50 (200k/1M/2M ms): selective 51/303/2,533; no_match
  52/307/2,538; exact_name 53/314/2,520; moderate 108/525/10,958; broad
  126/603/27,093; extension 107/410/37,578; worst_substring 124/523/19,591;
  prefix 110/532/10,762; short_prefix 139/573/29,272; mixed_case 133/579/21,120;
  unicode 122/584/11,849; high_limit_broad 127/560/28,669.
  - Scaling law: 200k->1M ≈ linear (x4-6 for x5 rows, all classes O(N) scan);
    1M->2M is a REGIME CHANGE, not algorithmic superlinearity: every 2M warm run
    shows `cold_rows_examined = 2,000,000` — the 2M projection (modeled 839.7MB,
    measured at forced eviction) exceeds the 500MB default budget, so all queries
    serve from the SQLite cold stream (2.6-37.6s).
  - `filename_index_used = false` for all 12 classes at every scale (structural —
    see the planner root cause below).

## Planner root cause (search scaling)

`FilenameCandidateIndices` unconditionally unioned `filenameNonAsciiEntries_` into
every candidate set. The fixture's ~10% Unicode rows alone (19.7k at 200k, 100k at
1M) exceed `kMaximumFilenameIndexCandidates = 16,384`, so every lookup returned
nullopt regardless of query shape. Fix (commit in this change): the candidate set is
the posting intersection alone — `FoldAscii` is length-preserving, non-ASCII names'
folded trigrams are indexed, so the intersection is already a conservative superset
(verified by `TestFilenameCandidatesAreConservativeSuperset` across ASCII/Unicode/
mixed-case/dotted shapes and the structural eligibility rules);
`TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows` reproduces the
cap pathology red-first and passes after the fix. Broad classes keep the canonical
scan by the existing cap gate (no forced index use).

## Test repairs discovered (red-first, `c261da4`)

- `Test-HookGuard.ps1`: `Start-Process -ArgumentList` path quoting — any checkout
  path with a space split `-File`; children exited 64; 9 checks failed; fixed at
  script scope (both call sites).
- `ffquick_actions_tests`: session-level clipboard read interception proven by raw
  Win32 probe (SetClipboardData sticks; GetClipboardData null for every format incl.
  raw CF_UNICODETEXT; OLE path fine) — round-trip block now discriminates genuine
  data loss (fails loud) from read interception (loud skip).

## Validation at the fix tree

Debug CTest 127/127 and release-test CTest 127/127 were green at `2e0d819`+`c261da4`
(two suites then repaired; identity-binding test compares the Debug preset's
binaries against current HEAD — Debug rebuilt at `c261da4`). Focused
indexstore/engine/search suites 18/18 green after the planner fix; full-suite
re-run at the planner-fix tree in progress at handoff.

## Planner-fix matrix (`search/search-matrix-planner-fix.json`, release-test at `243199e`)

Selective classes now use the accelerator (`filename_index_used=true`, rows
examined collapse from N to the candidate set); broad classes keep the canonical
scan by the cap gate (correct per the cost model). Result sets EXACTLY equal to
the baseline for all 24 class x scale pairs (zero regressions).

| class | 200k before -> after | 1M before -> after | rows examined (1M) |
| --- | --- | --- | --- |
| selective | 50.9 -> 1.2 ms (44x) | 302.5 -> 1.6 ms (189x) | 1 (was 1,000,000) |
| exact_name | 53.1 -> 1.0 ms (52x) | 313.8 -> 2.8 ms (114x) | 1 |
| no_match | 51.6 -> 1.5 ms (35x) | 307.4 -> 5.5 ms (56x) | 15,625 (was 1,000,000) |

Scan-class medians moved 0.6-1.0x across runs (uniform session variance: the
control run measured them at 1.0-1.3x of baseline with a code-identical scan
path; no scan regression is claimed or hidden). `no_match` candidates (15,625)
are trigram false positives (directory-count-shaped intersection), verified
empty of matches by the canonical matcher.

Acceptance (§17): linear scanning avoided for the clearly selective classes
(O(K) candidate verification replaces O(N) scans; 56-189x at 1M); no result-set,
Unicode, or cancellation regressions (24/24 exact result equality; the existing
cancellation/Unicode suites green).
