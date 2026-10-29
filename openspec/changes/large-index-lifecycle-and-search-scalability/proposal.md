# Proposal: large-index-lifecycle-and-search-scalability

## Problem

The completed `performance-closure-and-multisurface-acceleration` campaign left two
scaling questions open at production index sizes:

1. **Repeated replacement rebuilds are ~40x slower than the first build.** A 1M-row
   projection builds in ~4 s, but every subsequent full replacement
   (`Projection::RebuildVolumeFromStore` over an already-populated projection) takes
   ~159-164 s on the previous campaign's memory harness.
2. **Broad/prefix/substring search still scans every row**, and the filename trigram
   accelerator was measured (`filename_index_used = false` for all twelve query
   classes) on the previous campaign's search matrix.

A product that stays fast only for one benchmark run is not enough: FastFiles must
stay fast through reloads, rescans, searches, startup, and repeated lifecycle
operations at realistic index scale (200k / 1M / 2M rows).

## Findings (measured, not inferred)

- **Reproduction (Phase A):** `ffengine_bench_large_index_scalability --memory-report`,
  release-test, idle host: 1M first build 4,095.8 ms vs replacement cycles
  158,877.3 / 158,252.3 / 164,348.8 ms (~39x); 200k 2,444.8 ms vs 3,856-5,085 ms.
- **Root cause (differential isolation matrix, arms A-G):** per-entry
  filename-accelerator removal (`EraseSorted` posting erase, O(posting length)
  memmove per row per trigram) is **99.7% of removal cost** (200k: production
  removal 10,225.2 ms vs the same removal with accelerator maintenance skipped
  28.7 ms). `RebuildVolumeFromStore` discards the entire accelerator immediately
  after the removal (`CompactStorage` -> `InvalidateFilenameIndex`) and rebuilds it
  once at the end -- so the per-entry work is pure quadratic waste.
- **Production reachability:** startup `RebuildAll` runs on an empty projection (fast
  path, not the pathology). The same quadratic primitives are production-reachable
  through `ForgetVolume` (user command), reconciliation mass removal (journal gap /
  rescan), rule-prune removal loops, memory-pressure `EvictSubtree`/`EvictVolume`,
  and partial-eviction cold reload (`ReloadEvictedVolume` -> `RebuildVolumeFromStore`).
  All hold the pipeline mutex -> user-visible blocking.
- **Fix:** bulk removal (`Projection::RemoveBatch`) with one wholesale accelerator
  invalidation + one bulk rebuild above a 256-row threshold; small batches keep the
  per-entry path. Result: 1M replacement cycles 158,877/158,252/164,349 ms ->
  4,716/5,101/4,772 ms (**31-34x**, now at first-build cost); the removal arm at 200k
  10,225 -> 22.0 ms (**465x**). Memory unchanged (no double-buffer).
- **Memory-bound truth (Phase A collateral):** a 1M-row projection sits AT the
  default 500 MB budget edge (modeled 413.8 MB vs the 400 MB low watermark in the
  raw-projection harness; just under it through the pipeline harness) -- so 1M-row
  deployments can deterministically trip eviction at rebuild end. `SetProjectionMemoryBudget`
  has no production caller. Recorded as a lifecycle/memory-policy item (Phase C/D).
- **Search planner (Phase B, in progress):** `FilenameCandidateIndices` unconditionally
  unions every non-ASCII-named row into the candidate set; the fixture's 10% Unicode
  population (19.7k rows at 200k) alone exceeds `kMaximumFilenameIndexCandidates`
  (16,384), so every lookup returns nullopt and no query class can ever use the
  accelerator at 200k+ rows. `FoldAscii` is length-preserving and non-ASCII names'
  folded trigrams are indexed, so the union appears redundant for pure-ASCII needles
  (to be proven by the mandatory differential suite before any change).

## Approach

- Evidence-first: reproduce, isolate, classify reachability, then the narrowest safe
  fix; re-measure the same harness for exact before/after comparability.
- Bulk replacement stays in-place (no double-buffer), preserving memory-limit, LRU,
  eviction, index-correctness, and generation semantics by construction.
- Search: establish the 200k/1M/2M scaling law per query class first; then a small
  cost-based eligibility decision for the filename accelerator; differential
  equivalence is mandatory for every accelerated path.
- Test repairs discovered and fixed red-first along the way (hook-guard path quoting,
  quick-actions clipboard environment discrimination).

## Out of scope

- UI redesign; raw-volume synchronous completion bound (stays with
  `legacy-protocol-correctness-and-raw-volume-cancellation-closure`, externally gated);
  production signing/pins; hosted CI remediation (external).
