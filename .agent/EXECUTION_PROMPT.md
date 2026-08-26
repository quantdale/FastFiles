# NEXT CAMPAIGN — FASTFILES

Status: **ACTIVE — IMPLEMENTATION + FOCUSED DEBUG VALIDATION COMPLETE; full ladder (release-test/Analyze/ASAN/OpenSpec/exact-tree/hosted-CI) + durable 1M after-JSON pending**
Planned-From: `a1c8085b51f2594f9bf525414089c01fc43cd6b6`
Planned-At: `2026-08-26T21:00:00+08:00`
Target-Branch: `main`
Repository: `quantdale/FastFiles`
Campaign: **Indexed Search Zero-Copy Hot Path + Lock-Pressure Convergence**
OpenSpec change: `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`

## Mission

Remove the next proven production indexed-search bottleneck: the resident projection path currently constructs an owned `ffsearch::Candidate` and copies its interned UTF-16 name into `std::wstring` for every examined hot row while holding the engine state mutex, before the query matcher rejects most rows.

Do this without weakening the nonblocking indexed-read-plane contract, query-visible epoch semantics, cancellation, deterministic top-N ordering, projection memory limits, cold durable fallback, or any security/release invariant.

The campaign is NOT permission to blindly route production through `ExecuteSearchViews`. `CandidateView::name` currently points into `NamePool`'s contiguous vector arena, which can reallocate during `Intern`, while `Projection::CompactStorage` can replace the entire pool. A naked view escaping the projection lock would be unsafe. The executor must first prove a lifetime design, benchmark alternatives, and select the lowest-risk mechanism that makes rejected rows cheaper while keeping long-running matching outside the mutation critical section.

If the current post-optimization measurements show candidate ownership/name copying is no longer material, or every lifetime-safe implementation creates a larger lock/memory/churn regression, record the evidence and STOP rather than manufacture a rewrite.

## Planner reconciliation already performed

The preceding `committed-tree-validation-provenance-and-clean-checkout-parity` campaign was found implemented but stale in control-plane metadata. Its task ledger is fully checked. Commits `656acb8b8ebdc69da97501f34917df484213bf49` and `35deafe13e601b82bc88fc2e893f0df0b788a5f4` implement/correct the qualifier; hosted CI run `32956387761` succeeded for exact corrective SHA `35deafe13e601b82bc88fc2e893f0df0b788a5f4`. This planning wave records that completion in lifecycle/evidence and replaces the stale ACTIVE prompt with this campaign.

At planning time, current `main` was `a1c8085b51f2594f9bf525414089c01fc43cd6b6`; hosted CI run `32970997929` for that exact optimization-cleanup SHA was still in progress. Re-derive its actual result on execution resume; do not turn planner-time `in_progress` into PASS.

## Why this is the next frontier

The immediately preceding repository-wide optimization campaign already attacked the dominant matcher cost:

- mixed-case 1M-row production hit: 258.6 ms -> 85.9 ms (-67%);
- mixed-case 1M-row production miss: 122.7 ms -> 75.2 ms (-39%);
- prepared ASCII-fold needles now drive production literals and view matchers;
- the anchor path received a boundary/OOB fix;
- `ToWString` / `ToU16String` were changed from per-code-unit push loops to bulk construction;
- Debug/release-test were reported 86/86 and Analyze warning-clean locally.

The production hot loop still does this under `IndexPipeline::mutex_` for every examined resident row:

1. select a `ProjectionEntry` from the filename candidate list or projection cursor;
2. call `MakeProjectionCandidate`;
3. call `projection.Names().Get(entry.nameId)` and copy the name into `std::wstring`;
4. push the owned candidate into a bounded chunk;
5. release the mutex;
6. call `SearchAccumulator::Add`, where `MatchesWithoutFolder` rejects non-matches.

Existing timings/counters already expose `projectionRowsExamined`, `candidateNameCopies`, `candidateObjectsMaterialized`, `stateLockWaitMs`, `maxStateLockHoldMs`, matcher attribution, hot/cold rows, restarts, and durable fallback. The repository also already contains `CandidateView`, `Query::MatchesView*`, `ExecuteSearchViews`, per-name ASCII metadata, and filename candidate indices. This is therefore an evidence-rich seam, not speculative cleanup.

## Mandatory live reconciliation before edits

1. Read `AGENTS.md`, `AUTONOMOUS.md`, `CLAUDE.md`, `.agent/PLANNER_HANDOFF.md`, this prompt, `progress.md`, and `docs/agent/WORKSTATE.md`.
2. `git fetch --prune origin`; record branch, HEAD, origin/main, ahead/behind, worktree/index/untracked state, and recent commits.
3. Reconcile safely with current `main`. Never reset/stash/clean/discard user work automatically.
4. Inspect hosted CI for `a1c8085b51f2594f9bf525414089c01fc43cd6b6` and for the actual live tip if it changed.
5. Run/inspect project status, lifecycle integrity, `doctor`, and exact-tree provenance before changing product code.
6. Re-read the full new OpenSpec package under `openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/`.
7. Re-read at minimum:
   - `src/engine/src/IndexPipeline.cpp` and its header;
   - `src/indexstore/{include/ffindexstore/{NamePool.h,Projection.h,Store.h},src/{NamePool.cpp,Projection.cpp,Store.cpp}}`;
   - `src/search/{include/ffsearch/{Query.h,Search.h},src/{Query.cpp,Search.cpp}}`;
   - `tests/engine/{bench_large_index_scalability.cpp,bench_secondary_filename_index.cpp,test_index_pipeline.cpp,test_index_pipeline_concurrency.cpp}`;
   - `tests/indexstore/**` and `tests/search/**`;
   - the canonical `openspec/specs/indexed-read-plane/spec.md` and relevant historical read-plane/search changes.
8. Trace every production caller of `NamePool::Intern/Get`, `CandidateView`, `ExecuteSearchViews`, projection cursors/slot indices, `CompactStorage`, eviction/reload, and generation-validation logic. Do not assume the planner found every lifetime edge.
9. Inspect complete recent diffs for `b3b62e2`, `27f7d01`, and `a1c8085` so the new work does not undo the preceding optimizer's semantics or diagnostics.
10. Treat historical `ENGINEERING_AUDIT_REPORT.md` findings as stale leads only. Reproduce anything before acting on it.

## Workstream 1 — Establish the real post-optimization baseline

Before architecture changes, produce paired, reproducible measurements from the actual starting HEAD.

Use the existing benchmark infrastructure and add attribution only where needed. Cover at least:

- 1M-row resident mixed-case ASCII hit and miss;
- lowercase ASCII hit/miss;
- Unicode/fallback cases;
- sparse, medium, and broad match selectivity;
- filename-index eligible and ineligible simple literals;
- compound metadata/name filters;
- folder/path-dependent queries;
- relevance and explicit sort fields;
- low/high `maxResults`;
- all-hot and mixed hot/cold volume sets.

For each representative run record end-to-end time, rows examined, filename candidates, `candidateNameCopies`, `candidateObjectsMaterialized`, path lookups/materializations, matcher timing, state-lock wait/max hold, restarts/fallbacks, and transient memory. Use repeated runs and median + a tail statistic; do not cherry-pick one timing.

The benchmark MUST distinguish the just-fixed matcher cost from candidate-capture/name-copy cost. If materialization is not material anymore, write the evidence and stop the invasive campaign.

## Workstream 2 — Build the lifetime/invalidations authority map

Document precisely what can invalidate each borrowed identifier/view:

- `NamePool` arena data during `Intern`/reserve/growth;
- whole-pool replacement during `Projection::CompactStorage`;
- `ProjectionEntry` addresses during entry-vector growth/reorder/compaction;
- filename candidate indices during generation changes/compaction;
- parent-child spans;
- reconstructed path inputs;
- projection eviction/reload and bulk scan transitions;
- query-visible epoch, dependency-set epoch, residency/configuration snapshots.

Do not confuse semantic epoch validity with C++ object lifetime. A generation restart can reject stale results after the fact but cannot make a dangling pointer legal to dereference.

Add focused tests that prove the relevant invalidation model without deliberately executing UB.

## Workstream 3 — Evaluate implementation strategies before choosing one

Prototype/measure at least these classes of approach:

### A. Stable interned-name storage + lightweight row capture

Make individual interned-name byte ranges stable across ordinary `Intern`, then capture only row scalars + safe name handle/view in bounded chunks. If compaction/rebuild can replace storage, use an explicit bounded storage owner/lease so active searches never observe freed data.

Potential implementation shapes include a stable slab/chunk arena or another compact structure; do not default to one allocation per filename without measuring memory overhead and cache behavior.

### B. Generation-owned immutable name storage

Use a shared immutable storage generation that bounded search chunks/readers can retain while mutations advance the live projection. This is viable only if mutation does not copy the whole index per update and retired generation count/bytes are bounded and promptly reclaimed.

### C. Bounded compact capture rather than true zero-copy

If stable views make the projection materially more complex or memory-heavy, evaluate a chunk-owned compact representation: one contiguous name buffer plus lightweight row metadata, avoiding one `Candidate`/`std::wstring` object/allocation per rejected row. Call this copy-reduced, not zero-copy. Keep it only if measured lock/latency/memory results justify it.

Reject designs that:

- perform the whole matcher under `IndexPipeline::mutex_`;
- let a `std::u16string_view` escape a movable/replaced backing store;
- pin an O(N) mutable projection snapshot;
- create a whole-index candidate vector;
- weaken projection memory eviction/compaction;
- remove generation validation to make lifetime easier;
- make the cold durable reader depend on projection pointers;
- duplicate query parsing or result-order semantics.

Record the chosen design and rejected alternatives in `design.md` / evidence before full implementation.

## Workstream 4 — Implement view-first rejection with canonical semantics

For eligible resident rows, evaluate canonical view-compatible name/metadata predicates before constructing the full owned result candidate.

Required behavior:

1. Capture only lifetime-safe lightweight data under the mutation mutex.
2. Release the mutex before long-running predicate evaluation.
3. Use `Query::MatchesViewWithoutFolder` / `MatchesView` or a directly shared canonical predicate seam; do not implement a second parser/matcher.
4. Materialize an owned `Candidate` only after the row survives the view-compatible stage and ownership/path/ranking is actually needed.
5. Preserve lazy folder/path reconstruction.
6. Preserve `persistentOnly`, rule filters, scope, allocated-size knownness, match tier, sort, top-N/truncation, and deterministic tie-break order.
7. Keep explicit fallback for non-view-compatible queries or any case where safe lifetime cannot be established.
8. Preserve the filename secondary accelerator; candidate slot indices remain generation-scoped and must never outlive the corresponding projection state.

A row rejected by the optimized eligible path should not increment owned candidate/name materialization counters merely because it was examined.

## Workstream 5 — Preserve generation, churn, cancellation, and cold fallback

Revalidate every read-plane invariant against the new lifetime mechanism.

Adversarially cover:

- new row ingestion;
- rename/move/delete;
- metadata updates relevant and irrelevant to the query;
- projection compaction and memory-pressure eviction;
- cold volume reload;
- searchable volume/rule/residency configuration change;
- query-visible epoch advance while a captured chunk is matching;
- restart-budget exhaustion and durable SQLite fallback;
- per-client supersession/cancellation during capture, matching, materialization, generation validation, and durable streaming;
- engine/UI-server shutdown while readers/storage leases are alive.

Expected outcome remains exactly one coherent generation, a bounded restart, durable fallback, cancellation, or `Unavailable`. Never publish a mixed generation or partial result.

If the selected lifetime design retains name-storage generations/slabs, prove mutation progress is not blocked for the lifetime of a broad search and prove retired memory is bounded and reclaimed.

## Workstream 6 — Consolidate accumulation without semantic drift

`SearchAccumulator`, `ExecuteSearchCore`, and `ExecuteSearchViews` currently contain overlapping top-N/result behavior. Audit whether this campaign can safely share the result-acceptance/heap/order path without broad unrelated refactoring.

Only consolidate where it reduces real semantic drift risk. Do not turn this campaign into a search-library rewrite. The owned path should remain a trustworthy reference/fallback for deterministic parity tests.

## Workstream 7 — Permanent diagnostics and benchmark protection

Extend existing diagnostics minimally to show:

- lightweight hot rows captured;
- rows rejected before owned materialization;
- borrowed/copy-reduced/owned name counts as applicable;
- selected view-path/fallback reason;
- bounded capture time;
- view matching time;
- survivor materialization time;
- retained storage generation/lease count/bytes if the design introduces them.

Keep diagnostics path/name-free and optional in normal product use.

Extend existing benchmarks rather than creating a disconnected performance framework. Add a structural regression assertion where practical so future code cannot silently return to one owned name/candidate per rejected eligible row.

## Workstream 8 — Correctness and equivalence matrix

Use the canonical owned path as a deterministic test oracle. Compare optimized vs reference behavior for:

- mixed/lowercase ASCII;
- Unicode;
- empty/root names;
- duplicate basenames across directories/volumes;
- long/boundary names;
- glob, ext, kind, logical size, allocated size, modified date, folder/name filters and combinations;
- every scope;
- every sort field and direction;
- known/unknown allocated-size ordering;
- low/high result caps and exact truncation boundaries;
- rule filters;
- hot-only, cold-only, and mixed hot/cold composition;
- cancellation and generation changes.

Completed results must match exact candidate identities and exact order, not merely result counts.

## Workstream 9 — Performance acceptance gate

Correctness/lifetime safety are absolute. Performance must also justify the complexity.

The final selected production path must:

- structurally eliminate one owned `Candidate` + owned name copy for every rejected eligible hot row;
- show a repeatable material improvement on representative large resident selective queries in wall time and/or state-lock pressure;
- avoid a material regression on broad-match, complex-filter, Unicode, memory-pressure, and churn workloads;
- keep peak transient/retained memory bounded under the projection budget contract.

Treat roughly <=5% movement as noise unless repeated data establishes otherwise. Do not hide a wall-time regression behind improved counters. If the safe design fails this gate, revert the invasive product/storage change and retain only independently valuable tests/diagnostics/benchmarks.

## Workstream 10 — Secondary findings discipline

The planner found one low-priority latent note already recorded in workstate: `Store::cachedPageSize_` is unsynchronized mutable state but is currently used in a single-threaded maintenance context. Do NOT fold this into the campaign unless the live call graph proves concurrent calls are now possible. If that assumption changed, fix it narrowly with a focused test and record why scope expanded.

Do not resurrect old audit findings merely because they exist in `ENGINEERING_AUDIT_REPORT.md`; current code has undergone many hardening campaigns. Reproduce first.

## Required validation ladder

Run narrow to broad, using repository-authoritative commands discovered from current guidance/config:

1. new NamePool/projection lifetime tests;
2. `ffsearch_ascii_fold_tests` and focused query/search tests;
3. indexstore projection/name-pool/store tests affected by the storage representation;
4. `test_index_pipeline` and `test_index_pipeline_concurrency` focused suites;
5. large-index and filename-index benchmarks before/after;
6. strict OpenSpec validation;
7. source-provenance, doc-truth, diagnostic-policy, and project-status/release-frontier fixture suites;
8. `pwsh ./verify/verify.ps1 status`;
9. `pwsh ./verify/verify.ps1 doctor`;
10. official verifier build gate;
11. Debug configure/build + full CTest;
12. release-test configure/build + full CTest;
13. Analyze configure/build warning-clean;
14. repository-required ASAN protocol/IPC slice plus any new sanitizer-capable lifetime slice;
15. exact committed-tree qualification for the implementation commit;
16. `git diff --check` and full final diff review;
17. push without force, verify local HEAD/origin/main parity, and inspect hosted CI for the exact pushed implementation SHA.

Do not claim host-bound production signer/pins, privileged MFT/USN, provider/guest, clean-host, physical-display, or foreground-owned UIA evidence unless actually run in the required environment. Those remain external boundaries.

## Non-negotiable invariants

- Preserve the three-process privileged/unprivileged architecture.
- Preserve closed protocol and security boundaries.
- Preserve fail-closed Authenticode/pin behavior in production Release.
- Preserve degraded mode as a first-class permanent path.
- Preserve the canonical query language and UI-visible search behavior.
- Preserve `indexed-read-plane` bounded capture, nonblocking mutation progress, stable query-generation semantics, and durable fallback.
- Preserve projection memory ceiling/eviction/reload semantics.
- Preserve deterministic total result ordering and exact top-N/truncation behavior.
- Preserve per-client cancellation/lifecycle ownership.
- Preserve schema-v2 lifecycle/provenance and exact hosted-run SHA binding.
- Do not weaken tests, timeouts, memory caps, security checks, or release criteria to get green output.
- Do not force-push.
- Do not auto-reset, clean, stash, or discard user work.
- Do not add a network dependency to ordinary search/verification behavior.
- Do not hard-code an AI model/provider/harness into repository behavior.

## Git and delivery requirements

- Work from the actual current `main`; reconcile remote changes safely before implementation and again before final delivery.
- Commit logical waves with descriptive messages.
- Keep build trees, benchmark outputs, verifier runs/baselines/signing material, temporary DB/WAL/SHM files, and host artifacts out of Git.
- Maintain the new OpenSpec evidence with exact commands/results as implementation proceeds.
- Before final handoff, run exact-tree qualification against the commit actually intended for push/handoff, not only the dirty worktree.
- Push to `main` without force and inspect hosted CI for the exact pushed SHA.
- Update `docs/agent/WORKSTATE.md`, `progress.md`, `openspec/lifecycle.json`, the OpenSpec task/evidence ledger, and this prompt from actual final state.
- Mark this prompt `Status: **COMPLETE**` only after implementation/no-change decision, complete validation, exact-tree qualification, Git parity, and hosted-CI inspection are truthfully recorded.
- The final commit message must be a detailed engineering report: baseline, lifetime root cause, alternatives measured, chosen design, files/contracts changed, before/after data, correctness/concurrency matrix, validation, exact-tree/hosted evidence, external boundaries, final state, and next-frontier/STOP decision.

## Executor continuation contract

On `/goal continue` or equivalent:

1. Fetch/prune and derive live Git state.
2. Read this prompt plus the OpenSpec proposal/design/tasks/evidence.
3. Revalidate the premise from the current tree.
4. Resume from the first genuinely incomplete task; do not restart completed work.
5. Keep borrowed-memory lifetime and query-generation semantics explicit in every design change.
6. Fix campaign-caused Critical/High regressions before handoff.
7. If measurements disprove the performance premise, record the no-change outcome, mark tasks/evidence consistently, and STOP cleanly rather than forcing a risky implementation.

## Completion gate

This campaign is complete only when:

1. the current eager hot-row materialization cost has a fresh measured baseline;
2. projection/name/slot lifetime invalidations are explicitly mapped and regression-tested;
3. the selected implementation never dereferences a borrowed view after its storage lifetime;
4. eligible rejected hot rows avoid owned candidate/name materialization;
5. long-running matching remains outside the mutation critical section;
6. exact query/result/sort/truncation semantics match the canonical path;
7. churn/cancellation/compaction/eviction/restart/durable-fallback behavior remains fail-closed and deterministic;
8. any retained storage generations/leases are bounded and reclaimed;
9. paired performance evidence shows a material useful win or the invasive implementation is reverted with a documented no-change conclusion;
10. focused + full Debug/release-test/Analyze + required ASAN/policy/verifier gates pass;
11. OpenSpec/workstate/progress/lifecycle state reflects reality;
12. exact committed-tree qualification passes for the implementation/handoff commit;
13. the exact pushed SHA is inspected in hosted CI;
14. this prompt is marked COMPLETE and a truthful next-frontier/STOP result is recorded.
