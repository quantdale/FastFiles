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
2.5 ms (accelerated). That attribution is what justified the prefilter.

The survivors at 1M are exactly the fixture's non-ASCII name population
(98,438 = 9.8%): the conservative GLOB disjunct admits every name carrying
non-ASCII bytes (host-fold safety, see review-remediation P2-A). On a typical
corpus (1-2% non-ASCII names) the survivor count drops proportionally; the
hard floor is the SQLite name-column scan itself (~0.8-0.9 us/row measured),
so expected real-corpus cold selective latency at 1M is ~0.9-1.0 s vs 1.47 s
before. Run-to-run noise on the multi-second classes is up to ~10-20% (host
load; the untouched hot path moved by the same share between the two runs).

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
- Fixture: same synthetic 200k/1M corpora (10% non-ASCII names), same bench,
  medians over the harness's warm runs, both runs on the idle campaign host.
- Correctness: hot-vs-cold identity+ordering signatures (extended
  `TestForcedEvictionSearchEquivalence`), store-level superset + escaping
  red-first (`build/campaign/prefilter-red-run.log`), full suites green.
- Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
  non-ASCII, larger share expected on real corpora; zero schema/memory risk).
  The prior 56-189x hot acceleration and the 31-34x rebuild gain are
  unchanged (separate measurements below).
