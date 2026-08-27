# FastFiles Deep Audit — Completion Campaign 2026-08-27

Audited-Head: `57060c74e1d2416c8166201bbfd1834729ffd2da`  
Branch: `main`  
Repository: `quantdale/FastFiles`  
Planner-Time: `2026-08-27T21:54:00+08:00`  
Purpose: current-tree completion audit and 12-hour executor handoff.

## Executive verdict

FastFiles is advanced and heavily tested, but the current tree is **not locally complete**. The exact current SHA has failed hosted CI, the active indexed-search OpenSpec contains unresolved acceptance/evidence contradictions, and a separate durability failure-propagation seam remains the strongest next correctness investigation.

The correct next move is not another broad feature rewrite. It is a completion sequence:

1. repair current validation truth and benchmark attribution;
2. truthfully close or revert the indexed-search optimization under its existing acceptance contract;
3. open a bounded rule-pruning durability/reconciliation OpenSpec;
4. re-audit for the next locally reproducible correctness/security/concurrency frontier;
5. converge all local gates and lifecycle state to a clean exact final SHA.

## Audit coverage and method

The current recursive Git tree contains **1,154 tracked files** across **1,617 tree entries**, totaling about **24.56 MB logical tracked content**. Blob hashing yields **1,093 unique blobs**; 61 logical files are byte-identical duplicates, primarily multi-agent OpenSpec helper copies.

Tracked inventory includes:

- 506 Markdown files;
- 171 C++ source files;
- 133 headers;
- 52 PowerShell scripts and 43 PowerShell modules;
- 56 JSON files;
- 32 YAML files;
- 107 PNG evidence artifacts;
- root/CMake/policy configuration;
- protocol, IPC, setup/trust, privileged index service, engine, index store, search, UI, installer, tests, verifier/release tooling, OpenSpec history/evidence, audit artifacts, and vendored SQLite.

Top-level concentration: `openspec` 389 files, `src` 225, `codebase-audit` 142, `verify` 125, `tests` 116.

This pass uses an incremental whole-repository method. The prior deep audit at `9cec4f06...` enumerated/classified the full tracked tree and inspected critical source seams. Every unchanged blob remains byte-identical evidence from that audit. The delta from that audited baseline to current HEAD is only 11 paths, concentrated in the indexed-search implementation, its tests/OpenSpec/evidence, and agent/workstate control files; those current paths and their exact-SHA CI were re-inspected here. The current tree was independently re-enumerated and hashed to prevent path omissions.

This is an honest “every tracked file accounted for” audit, not a false claim that vendored SQLite, PNG evidence, or hundreds of historical Markdown pages were each manually reread line-by-line. Vendored SQLite is treated as an external dependency unless a dependency campaign is opened; historical evidence is never treated as current truth without SHA/reproducer reconciliation.

## Current architecture truth

FastFiles remains a Windows-native C++20 file manager with:

- privileged `FastFilesIndexSvc`;
- unprivileged `FastFilesEngine`;
- unprivileged UI `FastFiles`;
- closed named-pipe seams;
- fail-closed Authenticode/pinning and setup trust controls;
- durable SQLite index state plus resident projection;
- independent cold read sessions;
- generation-aware shared-memory/read-plane behavior;
- cancellable indexed search with deterministic ranking/caps;
- degraded-mode operation;
- substantial CTest, OpenSpec, PowerShell verifier, ASAN, Analyze, provenance, release-candidate, and hosted-CI coverage.

These are preserved constraints, not rewrite targets.

## Finding F1 — current exact SHA fails qualification

Severity: **High release/assurance blocker**  
Confidence: confirmed  
Exact run: `33033342573` for `57060c74e1d2416c8166201bbfd1834729ffd2da`

Observed hosted result:

- Analyze: PASS.
- ASAN protocol/IPC: PASS.
- unsigned release-candidate qualification: PASS.
- strict OpenSpec: 74/74 PASS inside hygiene.
- Debug: build succeeds, CTest 85/86; `ffproject_status_ps_tests` fails.
- release-test: build succeeds, CTest 85/86; same failure.
- hygiene: source/diagnostic policy and OpenSpec pass, then the same project-status test fails; exact-tree qualification is skipped.

Root cause is source-visible in `verify/status/tests/Test-ProjectStatus.ps1`: the live-tree assertion still expects all active local tasks to come from `openspec-lifecycle-evidence-integrity-and-semantic-convergence`, which is completed. The lifecycle model correctly reports the indexed-search campaign active with 11 active-local tasks.

Required repair: make the assertion lifecycle-driven and campaign-agnostic, with a fixture proving future active campaigns do not require editing this test.

## Finding F2 — active task ledger contains a false completion mark

Severity: **Medium assurance/truth blocker**  
Confidence: confirmed

Active task 7.3 was checked as “Run Debug configure/build + full CTest” while its own note said full CTest was still pending. Hosted exact-SHA evidence later shows the full suite fails.

The planner reopens this checkbox. Task truth must follow executed evidence, not intent or focused-suite success.

## Finding F3 — search timing attribution overlaps

Severity: **Medium performance-evidence defect**  
Confidence: confirmed

In `IndexPipeline::SearchIndexedReadPlane`, the current view-path code starts `matchStart` before iterating rows and adds the entire loop duration to `viewMatchingMs`. For accepted rows, that same interval includes `MakeProjectionCandidateFromLightweight`, `SearchAccumulator::Add`, and any lazy survivor materialization; `survivorMaterializationMs` separately times that survivor block.

Therefore the diagnostics do not currently partition capture → pure view match → survivor materialization. Any performance conclusion that treats those fields as independent attribution is unsound.

Required repair: make the timers non-overlapping or explicitly redefine/rename them and cover the accounting contract.

## Finding F4 — indexed-search keep/revert acceptance is unresolved

Severity: **High campaign-completion blocker**  
Confidence: confirmed

Current `design.md` acceptance says the selected safe implementation must show a **repeatable material improvement** on a representative resident selective workload, with roughly <=5% treated as noise unless repetition shows otherwise; if not, revert the invasive optimization.

Current implementation evidence reports the after-change wall time as **materially neutral**:

- selective about 2.62 ms vs baseline about 2.2 ms;
- moderate about 1954 ms vs about 2169 ms;
- broad about 4218 ms vs about 3854 ms;
- no-match about 10.8 ms vs about 17 ms;
- prefix about 2115 ms vs about 1946 ms;
- broad/prefix shifts are within baseline run-to-run spread.

The implementation undeniably eliminates owned `Candidate` + `std::wstring` materialization for rejected eligible rows, which is structurally useful. But the evidence document then treats that structural result as sufficient to retain the code, while the design's written acceptance threshold still requires repeatable material workload improvement.

This is a specification/evidence truth conflict. It must be resolved before task 6.4 or campaign COMPLETE. Do not silently move the goalpost after observing neutral latency.

## Finding F5 — secondary rule-pruning durable-write failures are not propagated consistently

Severity: **High potential durability/correctness risk; contract classification pending**  
Confidence: high on the seam, pending deterministic fault proof on user-visible impact

Current `IndexPipeline.cpp` patterns:

### MFT batch

The primary `store_.ApplyBatch(volumeId, changes)` failure returns `false`. After projection upserts and rule evaluation, a second `store_.ApplyBatch(volumeId, ruleExcluded)` only removes projection rows if it succeeds. If it fails, execution continues through memory enforcement/epoch advancement and returns `true`.

### USN batch

The same asymmetry exists: primary ApplyBatch failure returns false; rule-pruning ApplyBatch failure can still lead to success.

### Rebuild/open

Rule-excluded rows are removed durably only when the secondary ApplyBatch succeeds; failure skips pruning and the surrounding rebuild continues.

### Reconciliation

The reconciliation removal ApplyBatch is conditional. Regardless of its success, `reconciliationSeen_.erase(it)` executes, so a failed current cleanup can lose that pass's explicit seen-set repair obligation.

The `Store::ApplyBatch` contract itself documents rollback-on-failure so the caller can retry a batch. The higher-level intended contract still needs to be derived from current OpenSpec and callers.

Required next campaign: deterministic writer fault injection at secondary mutation boundaries, then classify atomic/fail-closed versus explicit eventual-recovery semantics. Test durable state, projection state, search/snapshot/aggregate visibility, return status, epoch/publication, restart, retry, and next reconciliation before choosing the repair.

## Finding F6 — NamePool lifetime/storage follow-up remains conditional

Severity: Low unless exposed by current/future storage changes.

The active design correctly recognizes that `NamePool` views are backed by movable storage and that `Projection::CompactStorage` can replace the pool. The current bounded name-buffer design avoids letting those views escape the capture lock. Task 2.2 still requests a focused non-UB test demonstrating the invalidation hazard; complete or explicitly retire that requirement.

If future work introduces generation-retained/slab storage, re-check 32-bit name offsets/lengths, memory accounting, dedup bucket overhead, compaction, Unicode-heavy names, and projection memory ceilings.

## Open PR/issues reconciliation

No open repository issues were returned by the current GitHub issue search. Draft PR #6 remains a historical stabilization branch from an older baseline and is already treated in repository lifecycle/workstate as superseded evidence; do not merge or cherry-pick it blindly over current main.

## Completion priorities

1. **F1/F2/F3:** restore validation truth and trustworthy performance evidence.
2. **F4:** close/revert indexed-search campaign under its actual acceptance gate.
3. **F5:** open and execute rule-pruning durability/reconciliation failure-closure OpenSpec.
4. Fresh audit: correctness/data integrity → security/IPC → concurrency/lifetime → resource bounds → UI state machines → verification false-greens → maintainability.
5. Final exact-tree convergence and hosted CI.

## Definition of “full completion” for this autonomous run

A truthful achievable local target is:

- zero active-local OpenSpec tasks;
- zero known locally reproducible Critical/High defects;
- no unresolved false-green/false-red validation contract;
- all repository-enforceable local gates green on the exact final SHA;
- hosted CI green on that exact SHA;
- lifecycle/docs/workstate synchronized;
- a fresh current-tree audit finds no evidence-backed local frontier worth opening.

External production signer/pins, unavailable clean-host/elevated-provider evidence, and physical-display/UIA evidence remain external. If those remain, the correct label is **locally complete; external certification pending**, not “100% production certified.”

## Executor entry point

Read `.agent/NEXT_CAMPAIGN_12H.md` first, then `.agent/EXECUTION_PROMPT.md` and the active OpenSpec. The orchestrator explicitly returns control to a fresh bounded OpenSpec after each completed/no-change campaign until the deadline or a genuine full-local-completion audit.
