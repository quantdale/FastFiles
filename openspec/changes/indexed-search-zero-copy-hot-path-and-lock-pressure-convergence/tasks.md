# Tasks

## 1. Reconcile live state and pin the baseline

- [ ] 1.1 Fetch/prune and record branch, HEAD, origin/main, ahead/behind, worktree/index/untracked state, recent commits, and hosted CI for the exact starting SHA.
- [ ] 1.2 Revalidate the completed `committed-tree-validation-provenance-and-clean-checkout-parity` implementation/evidence and this campaign's lifecycle entry before product edits.
- [ ] 1.3 Run the existing narrow search benchmarks and capture a new post-ASCII-fold 1M-row baseline with materialization and lock diagnostics.
- [ ] 1.4 Inventory all production callers of `Candidate`, `CandidateView`, `NamePool::Get/Intern`, projection cursors/slot indices, `CompactStorage`, eviction/reload, and search generation validation.

## 2. Prove the lifetime and mutation model

- [ ] 2.1 Document exactly which operations can invalidate projection entries, filename-index indices, `NamePool` views, and path reconstruction inputs.
- [ ] 2.2 Add focused tests demonstrating current `NamePool` view invalidation hazards across growth/compaction without invoking undefined behavior in the test itself.
- [ ] 2.3 Evaluate stable-name/slab, generation-owned storage, and bounded bulk-capture alternatives against memory, lock-hold, and implementation complexity.
- [ ] 2.4 Select one design only after measurements; record rejected alternatives and why they fail the indexed-read-plane invariants or performance gate.

## 3. Implement bounded lightweight hot-row capture

- [ ] 3.1 Introduce the minimum lifetime-safe row/name representation required to evaluate view-compatible predicates outside the mutation mutex.
- [ ] 3.2 Preserve `NamePool` deduplication correctness, memory accounting, reserve behavior, compaction/eviction semantics, and deterministic name lookup.
- [ ] 3.3 Route eligible resident projection searches through the canonical `Query::MatchesView*` predicates before owned `Candidate`/wstring materialization.
- [ ] 3.4 Materialize owned names/folders only for survivors that need result ranking, path/scope checks, filters, or response output.
- [ ] 3.5 Keep an explicit canonical fallback for non-view-compatible or unsupported lifetime cases; never silently change query semantics to obtain a fast path.

## 4. Preserve search-generation and read-plane semantics

- [ ] 4.1 Keep matching/sorting/path reconstruction outside `IndexPipeline::mutex_`; critical-section work remains bounded capture/validation only.
- [ ] 4.2 Preserve query-visible/dependency epochs, relevant-invalidations, bounded generation restarts, durable fallback, and fail-closed publication.
- [ ] 4.3 Preserve filename-secondary-index generation/slot safety and validate accelerator eligible/ineligible parity.
- [ ] 4.4 Preserve cold-volume SQLite read sessions and mixed hot/cold deterministic result ordering.
- [ ] 4.5 Preserve per-client cancellation, lifecycle shutdown, result caps, top-N ordering, and response framing behavior.

## 5. Add adversarial regression coverage

- [ ] 5.1 Prove exact result/order/truncation parity between old owned-candidate reference behavior and the selected optimized path across literal/filter/sort/scope cases.
- [ ] 5.2 Cover rename, move, delete, metadata update, MFT/USN ingestion, projection compaction, forced eviction/reload, volume rules, and residency changes during active search.
- [ ] 5.3 Cover cancellation during capture, view matching, survivor materialization, generation validation, and durable fallback.
- [ ] 5.4 Cover ASCII lower/mixed-case, Unicode, empty/root names, duplicate names, long names, sparse/broad matches, and filename-index boundary cases.
- [ ] 5.5 Add memory/lifetime stress proving any retained storage generation/lease is bounded and reclaimed after readers complete.

## 6. Performance evidence and diagnostics

- [ ] 6.1 Extend existing large-index/search benchmarks rather than creating a disconnected benchmark harness.
- [ ] 6.2 Record before/after wall time, candidate/name materializations, rows rejected pre-materialization, matcher attribution, max lock hold, lock wait, peak transient memory, restarts, and fallback use.
- [ ] 6.3 Demonstrate that rejected hot rows on the selected eligible path no longer require one owned `Candidate` plus one `std::wstring` name copy each.
- [ ] 6.4 Demonstrate a repeatable material improvement on the representative resident selective workload without a material regression on broad/complex/Unicode/churn workloads; otherwise revert the invasive optimization.

## 7. Full repository validation

- [ ] 7.1 Run focused search/query/indexstore/engine tests and the new lifetime/concurrency fixtures first.
- [ ] 7.2 Run strict OpenSpec validation plus documentation/source/provenance policy gates.
- [ ] 7.3 Run Debug configure/build + full CTest.
- [ ] 7.4 Run release-test configure/build + full CTest.
- [ ] 7.5 Run Analyze configure/build warning-clean.
- [ ] 7.6 Run the repository-required ASAN protocol/IPC slice and official verifier build gate.
- [ ] 7.7 Run exact committed-tree qualification on the implementation commit and inspect hosted CI for that exact pushed SHA.

## 8. Handoff and frontier decision

- [ ] 8.1 Update benchmark/evidence records with exact commands, machine/build configuration, raw counts, and before/after measurements.
- [ ] 8.2 Reconcile `docs/agent/WORKSTATE.md`, `progress.md`, lifecycle state, and `.agent/EXECUTION_PROMPT.md` from actual final reality.
- [ ] 8.3 Commit logical waves, fetch/prune before final delivery, push without force, and verify local/remote parity.
- [ ] 8.4 Mark this bounded OpenSpec campaign COMPLETE only after exact-tree validation and hosted-CI inspection. If the performance premise is disproven, record the no-change result and revert any unjustified invasive optimization; do **not** force code merely to satisfy the premise.
- [ ] 8.5 If `.agent/NEXT_CAMPAIGN_12H.md` is ACTIVE and its recorded 12-hour campaign deadline has not been reached, return control to that orchestrator after 8.4 instead of stopping: re-audit the live tree, select the next evidence-backed frontier, create the next bounded OpenSpec, and continue the autonomous campaign.
