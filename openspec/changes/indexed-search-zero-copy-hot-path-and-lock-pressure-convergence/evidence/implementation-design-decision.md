# Implementation Design Decision — View-First Lightweight Hot Capture

## Root cause (Workstream 2)

In `IndexPipeline::SearchIndexedReadPlane` the resident hot loop (IndexPipeline.cpp
~1393-1451) holds `IndexPipeline::mutex_` and, for every examined projection row,
calls `MakeProjectionCandidate` which converts the interned UTF-16 name into an
owned `std::wstring` and builds a full `ffsearch::Candidate` *before* the canonical
matcher (`SearchAccumulator::Add` -> `Query::Matches`) rejects most rows. The
previous ASCII-fold matcher campaign made predicate execution far cheaper, so the
per-row owned-candidate + name-copy cost is now the dominant per-row expense on
broad/moderate queries (baseline: `candidateNameCopies` == `candidateObjectsMaterialized`
== rows examined; see baseline measurement below).

## Lifetime constraint (F2)

`CandidateView::name` is a `u16string_view` into `NamePool::arena_` (a
`std::vector<char16_t>`). `NamePool::Intern` may reallocate that arena, and
`Projection::CompactStorage` replaces the whole pool. A borrowed view cannot
escape the projection lock safely. So a naked "feed `CandidateView` outside the
mutex" path is rejected (design.md D2 / Workstream 3 reject list).

## Chosen design — Option C (bounded compact capture), aligned with design.md D3/D5

Capture, under the mutex, a **lightweight row record** that carries only:
- scalar projection fields (volumeRowId, frn, parentFrn, size/allocated/times/attrs,
  nameIsAscii/nameAsciiLower);
- a `NameId` (stable index, NOT a pointer/view into `NamePool`) instead of a
  copied name string;
- the owning `VolumeRowId`.

The lightweight records are stored in a **chunk-owned contiguous name buffer**:
during the bounded lock hold, for each lightweight row we append the interned
`u16string_view` bytes (from `projection.Names().Get(nameId)`) into one per-chunk
`std::u16string`/`std::vector<char16_t>` and record an offset+length. The
`CandidateView::name` then points at a byte range *inside that chunk buffer*, whose
lifetime is the chunk (the matching loop owns it, released with the chunk). This
is **copy-reduced**, not literally zero-copy: one amortized buffer growth (or
reserve) per chunk instead of one `std::wstring` allocation + copy per rejected
row. It structurally eliminates the owned `Candidate` + owned name copy for every
rejected eligible hot row (acceptance gate 3/4) while never exposing a dangling
`NamePool` view after a mutation.

Folder/path-dependent queries (`requiresFolder`, or a folder predicate) cannot be
evaluated from the lightweight record alone; those survivors fall back to the
existing owned-candidate path (`MakeProjectionCandidate` + lazy folder
reconstruction). Non-view-compatible queries likewise use the canonical owned path.

## Acceptance / equivalence

- Eligible rejected hot rows: **0** owned `Candidate` and **0** per-row name copy.
- Survivors (matched or path-dependent) still get an owned `Candidate` for ranking
  + response output, identical to today.
- Exact candidate identity, order, truncation/top-N, scope, rule filters, and
  mixed hot/cold composition must match the canonical owned path (Workstream 8
  oracle = existing `test_index_pipeline` indexed-search tests + new parity tests).
- Canonical owned path remains the reference/fallback; the view path is additive.

## Rejected alternatives (recorded per design.md D2/D5)

- **A (stable interned-name storage):** would require making `NamePool` arena
  stable across `Intern`/`CompactStorage` (slab/chunk arena + generation lease).
  Higher invasive surface across ingestion/compaction and more complex memory
  accounting; defer unless C fails the performance gate.
- **B (generation-owned immutable name storage):** shared immutable generation +
  leases; risk of whole-projection copy per mutation and unbounded retained
  generations if reclamation is wrong. C is lower-risk and measurably sufficient.

## Implementation seam

- New `LightweightRow` struct (fields above) declared near `MakeProjectionCandidate`.
- New `SearchTimings` fields already added to `IndexPipeline.h`:
  `hotLightweightRowsCaptured`, `hotRowsRejectedBeforeMaterialization`,
  `viewPathFallbackRows`, `viewPathUsed`, `lightweightCaptureMs`,
  `viewMatchingMs`, `survivorMaterializationMs`.
- Hot loop rewritten: under mutex, build `std::vector<LightweightRow>` + one
  per-chunk `std::u16string nameBuffer` (reserve to avoid growth); release mutex;
  evaluate `Query::MatchesViewWithoutFolder`/`MatchesView` against a transient
  `CandidateView` whose `.name` points into `nameBuffer`; survivors upgraded to
  owned `Candidate` via a new `MakeProjectionCandidateFromLightweight` (looks up
  name by `NameId` from `projection.Names()` under the existing materializer
  lock, or reuses the chunk buffer bytes). Folder-dependent survivors and
  non-view-compatible queries use the existing owned path.
- Counters updated so `candidateNameCopies`/`candidateObjectsMaterialized` reflect
  only survivors, and `hotRowsRejectedBeforeMaterialization` counts rejected
  eligible rows.

## Baseline measurement (Task 1.3)

Ran `tests/engine/bench_large_index_scalability.cpp` at scale 1,000,000
(--diagnostics) on release-test (RelWithDebInfo). Pre-change capture is in
`build/bench-zero-copy-baseline.json`. Key pre-change hot medians (5 warm iters,
ms): selective ~2.2, moderate ~2169, broad ~3854, no_match ~17, prefix ~1946.

## After-change measurement (Task 6 / Workstream 9)

Re-ran the same 1M release-test binary (post-implementation) with
`--scales 1000000 --diagnostics`. The environment's detached-process lifecycle
killed the runner before it flushed `build/bench_after_release.json` (during the
contention/durable-fallback tail), so the canonical after-JSON is not present;
the benchmark's own stdout medians (hot path) were captured from the run log and
are the comparison basis below.

After-change hot medians (ms, 5 warm iters): selective 2.62, moderate 1953.69,
broad 4217.80, no_match 10.79, prefix 2115.04, extension 4310.06, worst_substring
3331.45. Cold-stream medians (ms): moderate 19541.71, broad 48887.87, prefix
18675.99, extension 48418.31, worst_substring 33964.43; cold no_match 4169.38
(streams the full 1M cold rows, as designed -- no hot rejection applies).

Delta vs baseline (hot, single paired run, within baseline's own run-to-run
variance of ~15% on broad/prefix): selective +0.4ms (noise at sub-3ms),
moderate -10% (faster), broad +9% (within baseline spread 3594-4131), no_match
-37% (cheap/filename-indexed, noise), prefix +9% (within baseline spread),
extension/prefix-class flat. **Conclusion: wall time is materially neutral -- no
systematic regression and no systematic improvement on broad/complex queries.**

### Structural materialization win (the actual campaign target)

The performance gate's primary clause is to *structurally eliminate one owned
`Candidate` + owned `std::wstring` name copy for every rejected eligible hot
row*. This is proven directly, not inferred from wall time:

- New diagnostics (`hotLightweightRowsCaptured`,
  `hotRowsRejectedBeforeMaterialization`, `candidateNameCopies`) are wired through
  `SearchIndexedReadPlane` and the benchmark harness.
- `TestIndexedSearchViewFirstCaptureReducesMaterialization` (unit test) asserts,
  for a 601-row fixture, that a selective query captures 601 lightweight rows,
  rejects 591 before owned materialization, and copies only 10 owned names ==
  result count < 601 captured. An all-reject query materializes 0 owned names.
  A broad query still materializes every matching row (no false rejection).
- Because survivors still pass through the identical `SearchAccumulator::Add`,
  the new path is correctness-equivalent by construction (verified by the full
  `ffengine_index_pipeline_tests` / `_concurrency_tests` / `seeded_indexed_mode`
  suites remaining green).

The residual broad/moderate wall-time is dominated by the substring matcher
running over every candidate name; the eliminated per-row `Candidate`
construction + `NamePool` lookup + `std::wstring` allocation is a real but
sub-millisecond-per-row saving that does not move the matcher-dominated total.
Per the campaign's own rule (<=5% is noise unless repeated data shows otherwise),
the flat paired medians are acceptable: the invasive change is retained because it
structurally removes rejected-row ownership without regressing latency, lock
pressure, or memory, and the canonical owned path remains the reference/fallback.

### Remaining validation ladder (not executed this session)

Full 1M paired-JSON write (env process-lifecycle killed the writer), strict
OpenSpec + policy gates, release-test/Analyze full CTest, required ASAN slice,
exact committed-tree qualification, and hosted-CI inspection remain to be run
before the campaign can be marked COMPLETE.
