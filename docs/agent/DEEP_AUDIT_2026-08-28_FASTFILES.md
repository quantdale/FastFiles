# FastFiles Deep Audit — Completion Integrity Review 2026-08-28

Audited-Head: `933528ed29c2deac583587bedf3259b8ee617f5d`  
Branch: `main`  
Repository: `quantdale/FastFiles`  
Planner-Time: 2026-08-28 (Asia/Manila)  
Hosted-CI: run `33095061127` = SUCCESS  
Decision: **Combined Implementation + Hardening Campaign**, correctness-first.

## Executive assessment

FastFiles is a mature Windows-native C++20 file manager with broad product functionality, substantial automated coverage, a three-process least-privilege architecture, durable SQLite indexing, resident/cold indexed search, file operations, Finder-style navigation, UI automation surfaces, release tooling, and a large OpenSpec history.

The repository is **not yet truthfully complete or production-certified**.

No evidence in this audit justifies another broad feature rewrite. The strongest work is concentrated in a small number of correctness and assurance gaps that survived a green exact-SHA CI run. The active rule-pruning durability change is only partially closed in reality: MFT and USN secondary-pruning failures now fail closed, and reconciliation implementation retains its in-memory obligation, but `RebuildAll` still reports/publishes rebuild completion after a secondary pruning failure, while the tests and task ledger overstate what has actually been proven.

The correct next phase is therefore:

1. finish the confirmed rule-pruning implementation gaps;
2. repair false-green OpenSpec/test evidence;
3. harden CI and release evidence retention;
4. run a fresh cross-subsystem regression/security/performance/UI review;
5. converge the exact final SHA through local-equivalent and hosted certification;
6. stop when only explicitly external certification remains.

A truthful terminal label, if external signer/clean-host/privileged/physical-display evidence is unavailable, is **locally complete; external certification pending**.

## Audit methodology and repository coverage

### Current tracked-tree accounting

The current recursive Git tree contains **1,160 tracked files** and GitHub reports the recursive tree as non-truncated.

Top-level concentration on the audited SHA includes:

- `src/`: 225 files
- `tests/`: 116 files
- `verify/`: 125 files
- `openspec/`: 394 files
- `codebase-audit/`: 142 files
- `docs/`: 27 files
- agent/harness integrations across `.agent`, `.claude`, `.cline`, `.codex`, `.cursor`, `.gemini`, `.opencode`, and related copies
- vendored SQLite under `third_party/sqlite`

The previous repository-wide audits at `9cec4f06...` and `57060c74...` enumerated and classified the complete tracked tree, including source, tests, build, verification, OpenSpec, documentation, release tooling, and vendor boundaries. This audit re-enumerated the current tree and reconciled the full delta from the last audited implementation baseline. From `57060c74...` to current HEAD there are only eight commits and eighteen changed paths, concentrated in:

- `src/engine/src/IndexPipeline.{h,cpp}`
- `tests/engine/test_index_pipeline.cpp`
- `tests/indexstore/test_projection.cpp`
- `tests/navigation/test_navigation_workspace.cpp`
- `verify/status/tests/Test-ProjectStatus.ps1`
- the indexed-search and rule-pruning OpenSpec/control-plane files
- agent planning/audit files

Those changed runtime/test/control-plane paths were freshly inspected against the exact current source.

This is an evidence-driven whole-repository audit, not a claim that binary PNG evidence or vendored SQLite was manually re-read byte-by-byte. Historical audit documents are evidence leads only. Vendored SQLite remains an external dependency unless a reproducible dependency defect is found.

### End-to-end seams traced

The review followed the important system paths rather than treating files independently:

- privileged index service -> MFT/USN records -> engine `IndexPipeline` -> SQLite `Store` -> resident `Projection` -> search/snapshot/aggregate consumers;
- rule configuration -> MFT/USN/rebuild/reconciliation pruning -> durable/projection publication and retry behavior;
- indexed search -> projection/cold-store capture -> query matching -> deterministic accumulation -> generation/cancellation;
- UI -> engine discovery/IPC -> browse/search/file operations -> UIA surfaces;
- source/build presets -> CTest -> Analyze/ASAN -> policy/status/provenance -> exact-tree qualification -> hosted CI;
- release-test build -> unsigned candidate packaging -> checksum/provenance validation -> artifact retention -> release-readiness composition;
- lifecycle/task ledgers -> generated project status -> release frontier.

## Current architecture truth

The current codebase preserves the intended architecture:

1. **FastFilesIndexSvc** — privileged LocalSystem broker for raw-volume indexing.
2. **FastFilesEngine** — unprivileged index/search/persistence coordinator.
3. **FastFiles** — unprivileged desktop UI.

The architecture uses closed named-pipe boundaries, Authenticode/pinning controls, durable SQLite state plus bounded in-memory projection, cold independent read sessions, generation-aware search, cancellable work, degraded mode, and an explicit separation between locally enforceable validation and environment-bound release evidence.

These are preservation constraints. The next campaign must not replace them with a broad rewrite.

## Evidence classification

### 1. Verified working on exact current SHA

Hosted CI run `33095061127` for `933528ed...` completed successfully.

Verified on that exact SHA:

- Debug configure/build + full CTest: **86/86 PASS**
- release-test configure/build + full CTest: **86/86 PASS**
- Analyze job: PASS / warning-clean build
- hosted ASAN slice: **2/2 PASS** (`ffprotocol_fuzz_tests`, `ffipc_framing_tests`)
- source marker policy: PASS
- diagnostic policy: PASS
- documentation truth + fixtures: PASS
- source-provenance fixtures: PASS
- project-status contracts: PASS
- provider contracts: PASS
- exact committed-tree qualification: PASS
- strict OpenSpec validation: PASS in hygiene
- unsigned release-candidate functional qualification: PASS
- current worktree in hosted checkout: clean

MFT and USN secondary rule-pruning code currently returns `false` when the second durable pruning batch fails. `FinishReconciliationPass` currently retains `reconciliationSeen_` when its removal batch fails and avoids a searchable epoch advance.

### 2. Implemented but insufficiently validated

- reconciliation failure-obligation retention;
- USN-specific secondary-pruning failure behavior;
- rebuild-specific secondary-pruning failure behavior;
- retry/repeated-failure semantics at every named boundary;
- shutdown/restart recovery for outstanding pruning debt;
- exact store/projection/search/snapshot/aggregate parity after every injected failure;
- fault-injection production isolation;
- current final-SHA three-test IPC/ASAN lifecycle slice;
- real release-readiness execution beyond unsigned validation;
- external signer/pins, elevated raw-volume operation, clean-host installation, physical-display/UIA certification.

### 3. Partially implemented

- `rule-pruning-durability-and-reconciliation-failure-closure`: MFT/USN and reconciliation implementation improved, but rebuild failure closure and required boundary/restart proof are incomplete.
- release qualification evidence retention: candidate validation runs, but hosted artifact upload retains no candidate/provenance files because the test writes to temporary directories while CI uploads from `out/release-candidate/**`.

### 4. Missing behavior or proof

- explicit boundary identity in the secondary-pruning fault injector;
- deterministic tests for MFT, USN, RebuildAll, and reconciliation as distinct boundaries;
- a real restart-after-failed-pruning regression;
- a direct shutdown/failure interaction regression for this subsystem;
- a fail-closed RebuildAll completion signal/dirty retry contract;
- current-SHA hosted retention of release candidate provenance artifacts.

### 5. Broken behavior or false-green assurance

See P1 findings below.

### 6. Obsolete/unnecessary work

- do not reopen completed broad UI/search/index campaigns without a current reproducer;
- do not merge historical PR/audit fixes blindly;
- do not rewrite the three-process architecture;
- do not re-optimize the indexed-search hot path unless new measurements show a current regression;
- do not manufacture local code changes to satisfy external signer/clean-host/physical-display evidence.

## Severity-ranked remediation matrix

| ID | Severity | Subsystem / files | Observed problem | Root cause | Impact | Required remediation | Verification / completion |
|---|---|---|---|---|---|---|---|
| FF-P1-01 | P1 High | `src/engine/src/IndexPipeline.cpp::RebuildAll`; rule-pruning OpenSpec | Secondary pruning failure during rebuild leaves rows in store/projection but the volume is still added to the rebuilt list, query-visible rebuild epoch is advanced, and `onVolumeRebuilt` is invoked. | Failure state is local to the pruning branch and is not propagated into rebuild completion. | Rebuild can be reported/published as complete while required exclusion pruning failed, violating the active durability spec. | Add explicit per-volume rebuild outcome/dirty obligation. Do not advance success publication/callback for a failed pruning boundary. Guarantee bounded retry/reconciliation. | Deterministic RebuildAll boundary injection; assert store/projection consistency, no false completion/epoch, retained debt; clear fault and prove convergence. |
| FF-P1-02 | P1 High | `tests/engine/test_index_pipeline.cpp::TestReconciliationRetentionOnSecondaryFailure` | The test comments out the key `hasRemove && hasKeep` assertion and contains a TODO saying it relaxes the check to let current behavior pass. It also configures the exclusion rule before seeding, allowing the row to be pruned before reconciliation. | Invalid fixture ordering plus weakened assertion. | Green CI does not prove the central reconciliation-retention contract; task 3.2 is a false green. | Rebuild the fixture so the removal definitely exists at FinishReconciliationPass. Assert precondition, durable/projection presence after injected failure, pass active, epoch unchanged, and successful retry. | Test must fail against intentionally broken retention and pass only with correct behavior. |
| FF-P1-03 | P1 High | active `tasks.md`, lifecycle/status | Mandatory tasks 3.1, 5.1, and 7.2 existed in the original active ledger but were deleted during the later task update rather than truthfully completed. Current status therefore reports zero active-local tasks while required proof is absent. | Task-ledger mutation collapsed the evidence surface instead of closing requirements. | Project-status can present a false completion frontier. | Restore the deleted requirements, reopen unsupported checkboxes, add a regression to prevent removal of still-required active tasks/evidence without an explicit spec revision. | Project status must show nonzero active-local work until requirements are actually evidenced; strict OpenSpec/status tests must remain green. |
| FF-P1-04 | P1 High | `IndexPipeline.h/.cpp`, engine tests | The fault injector is described as named-boundary but accepts only `(volume, changes)`; tests cannot select MFT vs USN vs rebuild vs reconciliation explicitly. Existing evidence extrapolates MFT behavior to other callers. | Fault seam lacks a boundary discriminator; coverage relies on call sequencing. | Distinct callers can regress while one generic test stays green. | Add an enum/boundary token to the test seam and build separate fixtures for all four boundaries. | One-shot and persistent tests at each boundary; ensure primary write behavior is unchanged. |
| FF-P1-05 | P1 High | rule-pruning tasks/tests | Task 5.3 claims cancellation/shutdown coverage using `TestCancellationDuringColdStream` and reconciliation `isCancelled` checks, but that search test is unrelated and the reconciliation fixture has no such cancellation path. Restart is required by the spec but not proven. | Evidence was inferred from adjacent tests rather than executed for the subsystem. | Outstanding pruning debt may behave incorrectly across Close/Open/restart without detection. | Add subsystem-specific Close/Open/rebuild/reconciliation recovery tests; classify cancellation semantics only where the API actually supports cancellation. | Fail/restart/reopen produces store/projection parity and a bounded successful prune without manual repair. |
| FF-P2-01 | P2 Medium | `.github/workflows/ci.yml`, `verify/release/tests/Test-ReleaseCandidate.ps1` | Release qualification passes, but artifact upload reports no files. The test writes candidates to temporary `FastFiles-release-candidate-test-*` directories while CI uploads `out/release-candidate/**`. | Producer and artifact-retention paths disagree. | Hosted CI loses candidate manifest/provenance evidence needed for later audit/release debugging. | Make the test emit/copy a safe summary to the workflow-owned output directory or parameterize output; upload exact known paths. | Final hosted run must upload candidate manifest + provenance with exact-SHA binding. |
| FF-P2-02 | P2 Medium | CI ASAN job, `tests/ipc/CMakeLists.txt` | Current hosted ASAN runs only protocol fuzz + IPC framing (2/2), while `ffipc_listener_lifecycle_tests` exists and prior campaign evidence described the required slice as 3/3. | CI sanitizer target list drifted/narrowed. | Listener lifecycle memory/lifetime regressions are not sanitizer-checked on every hosted SHA. | Reconcile the authoritative sanitizer policy. If listener test is sanitizer-compatible, restore it to hosted ASAN and exact qualification; otherwise document why and add equivalent lifetime coverage. | Exact final hosted SHA runs the authoritative sanitizer slice with no stale 3/3 claims. |
| FF-P2-03 | P2 Medium | `IndexPipeline.h/.cpp` | `SetSecondaryPruningFaultInjector` is public internal API and compiled unconditionally although design calls it test-only/guarded. | No test-build guard/friend harness separation. | Unnecessary production surface and spec mismatch. | Compile-guard or encapsulate the seam without weakening testability. | Release symbol/API review and all fault tests pass in test builds; production defaults/surface unchanged. |
| FF-P2-04 | P2 Medium | active OpenSpec evidence/tasks | Task 6.5 is checked while its note itself says hosted CI for the referenced implementation SHA was still in progress. Current HEAD CI is green, but evidence was never reconciled to exact current SHA. | Stale evidence text and premature checkbox. | Lifecycle truth becomes historical rather than exact-SHA truth. | Bind completion evidence to current final implementation SHA and run ID; update lifecycle/tasks only after observation. | No checked validation task may contain pending/in-progress evidence. |
| FF-P2-05 | P2 Medium / external | release-readiness lifecycle | Current exact-tree status reports release-readiness frontier SKIPPED; production signing/pins/clean-host/elevated raw-volume/direct-render evidence remains external. | Environment/provider prerequisites intentionally unavailable in ordinary CI. | Full production certification cannot be claimed locally. | Keep blocked-external state truthful; improve kits/instructions only if a concrete evidence-collection gap exists. | Final report separates local completion from external certification with explicit required artifacts. |
| FF-P3-01 | P3 Low | CI action/runtime maintenance | GitHub runner reports Node/action deprecation warnings during artifact steps. | Ecosystem transition. | Future CI maintenance risk, not a current product blocker. | Update only when supported action/runtime versions make the warning actionable; do not churn solely for cosmetic green logs. | CI remains supported and warning-free when practical. |

No P0/Critical defect was confirmed in this audit.

## Architectural observations

### Strengths to preserve

- privilege separation is explicit and security-aware;
- durable-first mutation ordering is established in ingestion paths;
- projection and cold-store search have generation/cancellation concepts rather than ad-hoc shared state;
- file-system identity uses NTFS identifiers rather than path-derived identity;
- query/search has canonical parsing and deterministic accumulation;
- verification is unusually comprehensive for a desktop application;
- documentation truth/provenance has executable checks;
- release tooling explicitly distinguishes unsigned validation from production certification.

### Current architectural weakness

The dominant weakness is not missing architecture. It is **semantic inconsistency at secondary mutation boundaries and evidence/control-plane drift**. A boolean failure in a secondary durable operation does not yet have one uniform completion/publication contract across MFT, USN, rebuild, and reconciliation, while the current tests/ledgers sometimes infer coverage instead of proving each caller.

The next campaign should fix that seam without widening into a storage rewrite.

## Product implementation gap assessment

The intended core product surface is already represented by current canonical specs and code for browsing/column view, home/quick access, tabs, dual pane, search/query UI, file operations/conflicts, drag/drop, preview/properties, storage analysis/treemap, settings/theming, keyboard commands, accessibility/UIA, indexed/degraded operation, index health, privilege diagnostics, installer/setup, and release verification.

This audit found no evidence requiring a new broad feature campaign before hardening.

Any newly discovered missing core feature during the fresh Wave 3 audit may open a bounded implementation OpenSpec, but only with a current reproducer and user-impact argument.

## Correct next phase

**Decision: Combined Implementation + Hardening Campaign.**

Rationale:

- Implementation is justified because FF-P1-01 through FF-P1-05 include concrete incomplete correctness behavior and missing proof inside the currently active durability change.
- Hardening is justified because the major product surface exists, current builds/tests are broadly green, and the highest residual value is in validation integrity, boundary-specific regression tests, release evidence, sanitizer coverage, security/concurrency review, and final certification.
- A pure feature campaign would ignore the strongest current risks.
- A pure hardening campaign would incorrectly treat RebuildAll failure behavior as already implemented.

Execution order is correctness-first: repair active rule-pruning -> repair truth surfaces -> CI/release evidence -> fresh cross-subsystem audit -> final certification.

## Validation matrix for the next campaign

| Gate | Minimum requirement |
|---|---|
| Focused durability | Separate MFT, USN, RebuildAll, reconciliation failure + retry tests |
| Restart | Inject failure, Close/Open/rebuild/reconcile, prove convergence |
| Visibility parity | Store, projection, indexed search, snapshot/browse, folder aggregate agree after failure and recovery |
| Epoch/publication | No success epoch/callback on failed rebuild/reconcile mutation; one coherent advance on successful recovery |
| Static/build | Debug, release-test, Analyze warning-clean |
| Unit/integration | Full CTest in Debug + release-test |
| Sanitizers | Authoritative protocol/IPC/lifecycle ASAN slice |
| OpenSpec | `openspec validate --all --strict --no-interactive` |
| Policy | source markers, diagnostics, documentation truth, provenance, project status, provider contracts |
| Exact tree | `Invoke-ExactTreeQualification.ps1` on committed final SHA |
| Packaging | unsigned candidate build/manifest/checksums/provenance |
| Evidence retention | hosted CI uploads candidate manifest/provenance |
| Security sanity | peer auth, command surface, frame limits, ACL/service rights, DLL loading, trust/pins unchanged/fail-closed |
| Performance sanity | no material regression in indexed search, rebuild/reconciliation, startup, large-index benchmark |
| UI/UX | major browse/search/file-op/settings/storage flows; UIA/keyboard/responsive checks available locally |
| External | signer/pins, elevated raw MFT/USN, clean host, physical display/UIA stay BLOCKED_EXTERNAL until real evidence exists |
| Git | clean worktree, local/remote parity, exact final SHA hosted CI green |

## Project completion definition

FastFiles may be called **locally complete** only when all of the following are true:

- zero known locally reproducible P0/P1 defects;
- the active rule-pruning OpenSpec is truthfully complete;
- no active-local task was removed, weakened, or checked without evidence;
- no material TODO/placeholder/disabled assertion remains on a core path;
- Debug + release-test full suites pass on the exact final source;
- Analyze and authoritative ASAN pass;
- strict OpenSpec and repository policy/provenance/status gates pass;
- exact committed-tree qualification passes;
- unsigned release-candidate qualification passes and evidence is retained;
- a fresh post-fix audit finds no higher-value local correctness/security/concurrency/data-integrity frontier;
- exact final pushed SHA has green hosted CI;
- lifecycle, WORKSTATE, progress, audit and agent prompts agree.

Production certification additionally requires the external release evidence declared by the repository. Those prerequisites must not be converted to PASS by code edits alone.

## Executor references

Primary execution contract after this audit:

- `.agent/NEXT_CAMPAIGN_12H.md`
- current active `openspec/changes/rule-pruning-durability-and-reconciliation-failure-closure/`
- queued completion package `openspec/changes/completion-integrity-hardening-and-release-certification/`

The queued completion package is dependency-ordered behind truthful closure of the current rule-pruning change. It may coordinate the remaining hardening/certification work without forcing a second simultaneous implementation frontier.
