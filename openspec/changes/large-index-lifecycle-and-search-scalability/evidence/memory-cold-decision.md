# Memory budget & cold-query characterization (tasks 8.2/19) — 2026-10-21

Measurement host: same campaign host, idle, release-test binaries at
`b6df9ef` (after) vs `420792d` (before). Same harness/command both sides:
`ffengine_bench_large_index_scalability --scales 200000,1000000 --diagnostics`
(`build/campaign/cold-profile-*.json` + console logs).

## 1. Policy map (verified against source; full map in the Agent-C audit)

- `Projection::kDefaultMaximumMemoryBytes` = **500 MiB**; the early-enforcement
  watermark (`EnforceMemoryLimit` at batch boundaries, `IsMemoryPressureHigh`)
  is `max - max/5` = **400 MiB**. Only `SetMaximumMemoryBytes` enforces the
  strict 500 MiB ceiling synchronously.
- `IndexPipeline::SetProjectionMemoryBudget` has **no production caller**
  (repo-wide grep: tests only). The production budget is therefore the fixed
  500 MiB default.
- Eviction: subtree victims preferred (oldest directory subtree), and
  `EvictSubtree` marks the **whole volume** cold. Reload
  (`ReloadEvictedVolume` -> `RebuildVolumeFromStore`) is a full-volume
  re-stream under the pipeline mutex (snapshot export path only; search never
  reloads). A volume larger than the low watermark can never reload (tested
  thrash behavior).
- Cold search streams `SELECT ... FROM entries WHERE volume_id=?` with no
  name predicate and no LIMIT; every cold row was decoded and matched in C++.

## 2. Cold-path profiling (the bottleneck, proven before changing anything)

At 1M rows (forced eviction; `coldRows` = rows reaching the C++ matcher):

| class | before: COLD ms / rows | after: COLD ms / rows | delta |
|---|---:|---:|---:|
| selective | 1470 / 1,000,000 | 1141 / 98,438 | 1.29x |
| no_match | 1413 / 1,000,000 | 1101 / 98,437 | 1.28x |
| exact_name | 1601 / 1,000,000 | 1115 / 98,438 | 1.44x |
| moderate | 6261 / 1,000,000 | 5817 / 196,875 | 1.08x |
| prefix | 6087 / 1,000,000 | 6449 / 196,875 | ~1.0 (noise) |
| broad | 17,680 / 1,000,000 | 14,885 / 393,749 | 1.19x |
| worst_substring | 10,921 / 1,000,000 | 10,498 / 295,313 | 1.04x |
| mixed_case | 11,891 / 1,000,000 | 11,476 / 295,312 | 1.04x |
| high_limit_broad | 16,337 / 1,000,000 | 16,120 / 393,749 | 1.01x |
| short_prefix | 16,354 / 1,000,000 | 15,357 / 1,000,000 | prefilter ineligible (<3) |
| extension | 19,971 / 1,000,000 | 21,758 / 1,000,000 | prefilter ineligible (predicate) |
| unicode | 6437 / 1,000,000 | 6237 / 1,000,000 | prefilter ineligible (non-ASCII needle) |

Before the change `stream` (the SQLite stream + decode + matcher) was >99.8%
of every cold class (e.g. selective 1468.5 of 1469.7 ms); hot selective stayed
2.5 ms (accelerated). Review-lane E E-6 correction: that percentage is a
timer-coverage tautology (the `storeStreamMs` timer wraps the entire
stream+match loop), not a scan/decode/match localization -- no such split was
instrumented. The prefilter case rests on the row-count mechanism
(decode/materialize 1M -> 98k rows), the before/after delta, and the
below-subtraction floor, not on the attribution percentage.

Measurement-discipline corrections (review-lane E E-1/E-2/E-3):
- Every cold cell is a **single measured sample** per class per scale (the
  bench runs one measured cold query after one warmup; hot figures are
  medians over 10 (200k) / 5 (1M) warm runs). "COLD" here means
  cold-projection with a warm SQLite page cache.
- The two runs were **not host-comparable on CPU-bound paths**: untouched hot
  medians moved 1.2-2.8x at 1M and up to 1.4x slower at 200k. The defensible
  noise control is the prefilter-ineligible cold classes measured in the SAME
  block (extension 19,971->21,758 ms; short_prefix 16,354->15,357; unicode
  6,437->6,237: -9%..+6%). Against that control the 1.28-1.44x literal-class
  deltas exceed observed drift and the row-count mechanism supports the
  direction, but each cell is one sample and the effect carries ~±10%
  uncertainty.
- Two-point subtraction on the no_match pair gives a survivor cost of ~0.35
  us/row and a **scan floor of ~1.05-1.10 us/scanned-row** (consistent at both
  scales) -- not the ~0.8-0.9 us/row previously written. Even a zero-survivor
  literal is therefore ~1.05-1.1 s at 1M; on a 1-2% non-ASCII corpus (an
  unmeasured assumption; CJK-heavy corpora fall back toward the measured
  9.8%-survivor case) cold selective at 1M is ~1.1 s vs 1.47 s before
  (~1.3-1.4x).
- Provenance note (lane E E-8): the two cold-profile artifacts do not embed
  source identity; the `420792d`/`b6df9ef` pairing is asserted provenance
  (consistent with observable data such as the durable-fallback row drop),
  and future runs should embed `--source-sha` like the memory-report does.

The survivors at 1M are exactly the fixture's non-ASCII name population
(98,438 = 9.8%): the conservative GLOB disjunct admits every name carrying
non-ASCII bytes (host-fold safety, see review-remediation P2-A). The hard
floor is the SQLite name-column scan itself (~1.05-1.10 us/row, see the
corrections above), so expected real-corpus cold selective latency at 1M is
~1.1 s vs 1.47 s before -- about 1.3-1.4x. Run-to-run noise between the two
runs reached 2.8x on untouched hot paths (host state differed); the
same-block ineligible-class control bounds the drift at -9%..+6% for the
cold table.

## 3. The 2M regime (task 8.2 finding, characterized)

The 2M-row projection models ~839.7 MB > the 500 MiB default, so every 2M
query serves from the cold stream under the default budget (historical
search-matrix p50 at 2M: selective ~2.5 s, broad ~27.1 s, extension ~37.6 s;
the prefilter does not change non-literal classes and removes only the
survivor decode share for literals). Serving 2M rows hot would require a
budget near 1 GiB -- not raised here (see decision).

## 4. Product decision (mission 19): **Option A + E, with the cold prefilter
## shipped as the evidence-backed improvement**

- **Option A (retained):** keep the 500 MiB default; accelerate the cold path
  within existing semantics. Shipped: `ForEachEntryWithAsciiNameUntil`
  conservative SQL name prefilter (commit `b6df9ef`), superset-proven
  (LIKE-ASCII-fold + non-ASCII-bytes disjunct; red-first on the LIKE-escape
  under-match class) and measured above. No schema change, no new memory.
- **Option B (adaptive budget) — rejected for this change:** an adaptive
  policy needs floors/ceilings and behavior proofs under changing available
  RAM; none of the measured evidence requires it yet, and the mission forbids
  raising default consumption without safety proof.
- **Option C (hybrid hot/cold residency) — deferred as separately scoped
  architecture work:** eviction currently marks a whole volume cold and the
  reload path re-streams under the mutex; a compact searchable subset would
  need strict consistency + memory-bound proofs (a new OpenSpec change).
- **Option D (explicit setting) — rejected:** `SetProjectionMemoryBudget` has
  no production caller and no settings surface/persistence semantics exist;
  a half-wired setting would be a worse product than none.
- **Option E (document the limitation) — adopted:** the 2M-entry cold regime
  is documented precisely above; the remaining candidate improvement (a
  durable trigram/FTS candidate table, schema v3 + migration + superset proof)
  is deliberately NOT taken here because the mission requires separate schema
  justification and the measured 1.3-1.7x cold-literal gain does not justify
  that risk inside this campaign.

## 5. Retention classification (mission 23) for the one performance change

- Candidate: cold SQL name prefilter (`b6df9ef`), baseline `420792d`.
- Fixture: same synthetic 200k/1M corpora (10% non-ASCII names), same bench.
  Hot figures are medians over the harness's warm runs; every cold figure is
  a single measured sample per class per scale (lane E E-2), on runs whose
  CPU-bound paths were not host-comparable (E-1); see the corrections in §2.
- Correctness: hot-vs-cold identity+ordering signatures (extended
  `TestForcedEvictionSearchEquivalence`), store-level superset + escaping
  red-first (`build/campaign/prefilter-red-run.log`), full suites green.
- Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
  non-ASCII vs a -9%..+6% same-block control, ~1.3-1.4x expected on real
  corpora; zero schema/memory risk). Scope conditions on the prior hot
  acceleration (lane E E-5): the 56-189x figure is hot/projection-resident,
  planner-eligible literal needles only (best case 189x = 1-result ASCII
  literal at 1M; rows examined 1M->1), it repairs a planner bug that had
  disabled the accelerator entirely, and it does NOT apply to cold queries
  or the >=2M regime, where all queries serve from the durable stream and
  latency is governed by the scan floor above. Lane E E-7: the 1M cold table
  characterizes the production cold-stream cost model under a state that at
  1M rows requires multi-volume memory pressure to reach (the 1M projection
  models ~318 MiB, below the 400 MiB watermark); the common production cold
  regime is >=2M rows.
