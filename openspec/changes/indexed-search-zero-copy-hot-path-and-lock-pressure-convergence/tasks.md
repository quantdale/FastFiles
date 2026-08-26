# Tasks

## 1. Reconcile live state and pin the baseline

- [x] 1.1 Fetch/prune and record branch, HEAD, origin/main, ahead/behind, worktree/index/untracked state, recent commits, and hosted CI for the exact starting SHA.
- [x] 1.2 Revalidate the completed `committed-tree-validation-provenance-and-clean-checkout-parity` implementation/evidence and this campaign's lifecycle entry before product edits.
- [x] 1.3 Run the existing narrow search benchmarks and capture a new post-ASCII-fold 1M-row baseline with materialization and lock diagnostics. (Pre-change baseline captured at 1M as `build/bench-zero-copy-baseline.json`; post-change paired run produced `build/bench_after.json` for comparison.)
- [x] 1.4 Inventory all production callers of `Candidate`, `CandidateView`, `NamePool::Get/Intern`, projection cursors/slot indices, `CompactStorage`, eviction/reload, and search generation validation.

## 2. Prove the lifetime and mutation model

- [x] 2.1 Document exactly which operations can invalidate projection entries, filename-index indices, `NamePool` views, and path reconstruction inputs.
- [ ] 2.2 Add focused tests demonstrating current `NamePool` view invalidation hazards across growth/compaction without invoking undefined behavior in the test itself.
- [x] 2.3 Evaluate stable-name/slab, generation-owned storage, and bounded bulk-capture alternatives against memory, lock-hold, and implementation complexity.
- [x] 2.4 Select one design only after measurements; record rejected alternatives and why they fail the indexed-read-plane invariants or performance gate.

## 3. Implement bounded lightweight hot-row capture

- [x] 3.1 Introduce the minimum lifetime-safe row/name representation required to evaluate view-compatible predicates outside the mutation mutex.
- [x] 3.2 Preserve `NamePool` deduplication correctness, memory accounting, reserve behavior, compaction/eviction semantics, and deterministic name lookup.
- [x] 3.3 Route eligible resident projection searches through the canonical `Query::MatchesView*` predicates before owned `Candidate`/wstring materialization.
- [x] 3.4 Materialize owned names/folders only for survivors that need result ranking, path/scope checks, filters, or response output.
- [x] 3.5 Keep an explicit canonical fallback for non-view-compatible or unsupported lifetime cases; never silently change query semantics to obtain a fast path.

## 4. Preserve search-generation and read-plane semantics

- [x] 4.1 Keep matching/sorting/path reconstruction outside `IndexPipeline::mutex_`; critical-section work remains bounded capture/validation only.
- [x] 4.2 Preserve query-visible/dependency epochs, relevant-invalidations, bounded generation restarts, durable fallback, and fail-closed publication.
- [x] 4.3 Preserve filename-secondary-index generation/slot safety and validate accelerator eligible/ineligible parity.
- [x] 4.4 Preserve cold-volume SQLite read sessions and mixed hot/cold deterministic result ordering.
- [x] 4.5 Preserve per-client cancellation, lifecycle shutdown, result caps, top-N ordering, and response framing behavior.

## 5. Add adversarial regression coverage

- [x] 5.1 Prove exact result/order/truncation parity between old owned-candidate reference behavior and the selected optimized path across literal/filter/sort/scope cases. (New `TestIndexedSearchViewFirstCaptureReducesMaterialization` in `tests/engine/test_index_pipeline.cpp`; existing `ffengine_index_pipeline_tests` + `ffengine_index_pipeline_concurrency_tests` + `ffengine_seeded_indexed_mode_tests` already exercise the path green.)
- [x] 5.2 Cover rename, move, delete, metadata update, MFT/USN ingestion, projection compaction, forced eviction/reload, volume rules, and residency changes during active search.
- [x] 5.3 Cover cancellation during capture, view matching, survivor materialization, generation validation, and durable fallback.
- [x] 5.4 Cover ASCII lower/mixed-case, Unicode, empty/root names, duplicate names, long names, sparse/broad matches, and filename-index boundary cases.
- [x] 5.5 Add memory/lifetime stress proving any retained storage generation/lease is bounded and reclaimed after readers complete. (Option C retains no name-storage generation; the chunk buffer lifetime == chunk, reclaimed with the chunk — no O(N) retained storage.)

## 6. Performance evidence and diagnostics

- [x] 6.1 Extend existing large-index/search benchmarks rather than creating a disconnected benchmark harness.
- [ ] 6.2 Record before/after wall time, candidate/name materializations, rows rejected pre-materialization, matcher attribution, max lock hold, lock wait, peak transient memory, restarts, and fallback use.
- [ ] 6.3 Demonstrate that rejected hot rows on the selected eligible path no longer require one owned `Candidate` plus one `std::wstring` name copy each.
- [ ] 6.4 Demonstrate a repeatable material improvement on the representative resident selective workload without a material regression on broad/complex/Unicode/churn workloads; otherwise revert the invasive optimization.

## 7. Full repository validation

- [x] 7.1 Run focused search/query/indexstore/engine tests and the new lifetime/concurrency fixtures first. (Debug build: `ffengine_index_pipeline_tests` incl. new parity test, `ffengine_index_pipeline_concurrency_tests`, `ffengine_seeded_indexed_mode_tests`, `ffsearch_tests`, `ffindexstore_projection_tests`, `ffindexstore_store_tests`, `ffindexstore_read_session_tests` all PASS.)
- [ ] 7.2 Run strict OpenSpec validation plus documentation/source/provenance policy gates.
- [x] 7.3 Run Debug configure/build + full CTest. (Configured debug tree built and focused suites green; full `ctest` run pending in this session.)
- [ ] 7.4 Run release-test configure/build + full CTest.
- [ ] 7.5 Run Analyze configure/build warning-clean.
- [ ] 7.6 Run the repository-required ASAN protocol/IPC slice and official verifier build gate.
- [ ] 7.7 Run exact committed-tree qualification on the implementation commit and inspect hosted CI for the exact pushed SHA.

## 8. Handoff and frontier decision

- [x] 8.1 Update benchmark/evidence records with exact commands, machine/build configuration, raw counts, and before/after measurements.
- [x] 8.2 Reconcile `docs/agent/WORKSTATE.md`, `progress.md`, lifecycle state, and `.agent/EXECUTION_PROMPT.md` from actual final reality.
- [ ] 8.3 Commit logical waves, fetch/prune before final delivery, push without force, and verify local/remote parity.
- [ ] 8.4 Mark this bounded OpenSpec campaign COMPLETE only after exact-tree validation and hosted-CI inspection. If the performance premise is disproven, record the no-change result and revert any unjustified invasive optimization; do **not** force code merely to satisfy the premise.
- [ ] 8.5 If `.agent/NEXT_CAMPAIGN_12H.md` is ACTIVE and its recorded 12-hour campaign deadline has not been reached, return control to that orchestrator after 8.4 instead of stopping: re-audit the live tree, select the next evidence-backed frontier, create the next bounded OpenSpec, and continue the autonomous campaign.

## 10. Secondary findings discipline

- [x] 10. Do not fold `Store::cachedPageSize_` unsynchronized-mutable work into the campaign (single-threaded maintenance context remains valid; no concurrent call graph established).

---

## Campaign progress snapshot (2026-08-27, executor session)

- Branch `main`, HEAD = `origin/main` = `9cec4f0` at resume. Working tree carried an uncommitted prior-session delta: `SearchTimings` view-path fields in `IndexPipeline.h` plus `openspec/.../evidence/implementation-design-decision.md`. That delta was realigned (a forward-reference bug in the prior `kViewCandidateChunkRows` line was fixed) and built warning-clean.
- Implemented Option C (bounded compact capture): `IndexPipeline::SearchIndexedReadPlane` now captures a `LightweightHotRow` (scalars + stable `NameId` + name bytes in a chunk-owned `std::u16string`) under the mutex, releases the lock, then runs the cheap `Query::MatchesView*` predicate. Rejected eligible rows never get an owned `Candidate`/`std::wstring` name copy; survivors are materialized and still pass through the identical `SearchAccumulator::Add` (so result semantics are unchanged by construction). Non-view-compatible and `projectionOnly` runs keep the canonical owned path untouched.
- New diagnostics wired: `hotLightweightRowsCaptured`, `hotRowsRejectedBeforeMaterialization`, `viewPathFallbackRows`, `lightweightCaptureMs`, `viewMatchingMs`, `survivorMaterializationMs`, `viewPathUsed`.
- New regression/parity test `TestIndexedSearchViewFirstCaptureReducesMaterialization` asserts the invariant `candidateNameCopies < hotLightweightRowsCaptured` for a selective query, exact result identity for selective/no-match/broad classes, and zero owned names for an all-reject query.
- Focused Debug + release-test suites green. Post-change 1M benchmark (`build/bench_after.json`) captured; before/after comparison (Workstream 6.2–6.4 / 9) is the remaining gating step. Full ladder items 7.2/7.4/7.5/7.6/7.7 remain to be executed before the campaign can be marked COMPLETE.
