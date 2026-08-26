# Proposal — Indexed Search Zero-Copy Hot Path and Lock-Pressure Convergence

## Why

The repository-wide optimization campaign completed on 2026-08-26 removed the dominant mixed-case literal-matching cost on the production projection path. The next visible hot-path seam is now candidate ownership rather than predicate dispatch.

`IndexPipeline::SearchIndexedReadPlane` currently creates an owned `ffsearch::Candidate` for every examined resident projection row while holding the engine state mutex. `MakeProjectionCandidate` copies the interned UTF-16 name into `std::wstring`; only after the bounded chunk is released from the mutex does `SearchAccumulator::Add` run the canonical query matcher and reject most non-matches. Existing diagnostics already count `candidateNameCopies`, `candidateObjectsMaterialized`, state-lock wait/hold time, and rows examined.

The repository already contains a projection-oriented `CandidateView` matcher with prepared literal fast paths. However, `CandidateView::name` points into `NamePool`, whose contiguous `std::vector<char16_t>` arena can move during `Intern`, and `Projection::CompactStorage` can replace the entire pool. A view cannot simply escape the mutation lock. The existing `indexed-read-plane` specification also requires long-running matching to occur outside the mutation/lifecycle critical section.

This campaign closes that architectural gap: reduce or eliminate rejected-row ownership/name-copy work on the hot resident path while retaining coherent epochs, bounded lock holds, cancellation, cold durable fallback, deterministic top-N sorting, memory limits, and exact behavioral parity.

## What Changes

1. Establish a post-ASCII-fold baseline at the actual live HEAD with materialization, lock, matcher, and end-to-end latency attribution.
2. Prove the lifetime model for projection entries, interned names, filename-index slot indices, compaction, memory eviction, rebuilds, and query-visible epochs.
3. Implement the lowest-risk bounded capture/read mechanism that allows view-compatible predicates to reject rows before owned `Candidate`/name materialization without keeping borrowed storage alive unsafely.
4. Keep non-view-compatible, folder-dependent, Unicode, cold-volume, and durable-fallback paths semantically identical and explicitly benchmarked.
5. Add adversarial concurrency tests covering ingestion, rename/move/delete, compaction/eviction, volume configuration/residency changes, cancellation, and generation restart/fallback.
6. Convert the optimization into permanent diagnostics and benchmark/regression coverage rather than relying on one local timing.

## Non-Goals

- Parallel full-index scanning or thread-per-chunk search.
- Moving unbounded matcher/sorter work into the engine mutation mutex.
- Reworking MFT/USN parsing, `VolumeScanner`, snapshot serialization/publication, SQLite ingestion, or degraded enumeration; the immediately preceding investigation found no justified optimization there.
- Product/UI redesign, new search syntax, or changes to relevance/sort semantics.
- Production signing/pin provisioning, privileged live MFT/USN certification, provider/guest, clean-host, or foreground-owned UIA evidence.
- PCH/build-time work unless separately measured and planned.

## Success Criteria

The campaign is successful only if all of the following are demonstrated on the final implementation tree:

- rejected hot projection rows on the selected view-compatible path do not require an owned `Candidate` or per-row `std::wstring` name copy;
- no borrowed projection/name view survives a storage mutation without an explicit lifetime guarantee;
- long-running matching/sorting/path work remains outside the mutation critical section;
- query-visible epoch and durable-fallback semantics remain fail-closed under churn;
- exact result sets, ordering, truncation, cancellation, and diagnostics match the canonical path across the regression matrix;
- a representative large resident-index benchmark shows a material improvement in either end-to-end latency or state-lock pressure without a material regression on broad/complex/Unicode cases;
- Debug/release-test/Analyze and the repository's required robustness/policy gates remain green.

If reconnaissance proves the current ownership cost is no longer material, or every safe design regresses lock pressure/memory enough to erase the benefit, record the evidence and stop rather than shipping architectural complexity for an unmeasured win.
