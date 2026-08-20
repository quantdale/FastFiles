# Tasks — allocated-size-search-filter

Baseline when authored: `main` at `3ad54ae01bb664623d16511bfb5e99761e431743`. Live Git state wins on resume.

Do not mark a task complete without executable evidence. Do not reopen the parked privileged production-validation campaign.

## 1. Reconcile and baseline

- [x] 1.1 Fetch/prune `origin`, reconcile `main`, verify the starting tree is clean, and read `AGENTS.md`, `docs/agent/WORKSTATE.md`, this change, `indexed-search-allocated-size`, and canonical `search-query-engine` / `search-ui` specs.
- [x] 1.2 Reconcile the stale top-level WORKSTATE status left at `3ad54ae`: preserve the prior live evidence but stop saying its final UIA/build acceptance is pending after that evidence was completed.
- [x] 1.3 Trace `FilterRegistry::WithDefaults`, `ParseSizeFilter`, owning/view predicate factories, `PredicateKind`, `Query::Matches*`, `IndexPipeline::SearchIndexed`, degraded Search candidate creation, Search query feedback/status, and the live Search UIA scenarios.
- [x] 1.4 Run focused existing query/search tests before modification and record the baseline.

## 2. Registry and parser

- [x] 2.1 Register exactly one new default filter key: `ondisk`.
- [x] 2.2 Reuse/refactor the existing `SizeFilter` numeric parser so `size:` and `ondisk:` share the same operators, range syntax, overflow checks, and B/KB/MB/GB units.
- [x] 2.3 Ensure malformed `ondisk:` values use the existing invalid-filter reporting path.
- [x] 2.4 Preserve the current unrecognized-key behavior for spellings other than the registered `ondisk` key; do not add an `allocated` alias.
- [x] 2.5 Add/adjust predicate diagnostic attribution so allocated-size metadata filtering is distinguishable from logical-size filtering if diagnostics currently report predicate kinds.

## 3. Predicate semantics

- [x] 3.1 Implement owning `Candidate` `ondisk:` evaluation using `allocatedSizeKnown` + `allocatedSizeBytes` only.
- [x] 3.2 Implement equivalent `CandidateView` evaluation so the accelerated projection path does not fall back to owning-only execution.
- [x] 3.3 UNKNOWN (`allocatedSizeKnown=false`) SHALL return false for every numeric `ondisk:` predicate regardless of the stored byte field.
- [x] 3.4 Known zero SHALL remain numeric zero and match exact/appropriate comparison/range predicates.
- [x] 3.5 Preserve `size:` predicates byte-for-byte/behaviorally as logical-size predicates; do not route them through allocated metadata.
- [x] 3.6 Ensure `ondisk:` is metadata-only and does not set `requiresFolder` or otherwise force path reconstruction.

## 4. Deterministic query-engine tests

- [x] 4.1 Add exact known-nonzero tests: `ondisk:8MB` and `ondisk:=8MB`.
- [x] 4.2 Add comparison tests for `>`, `>=`, `<`, `<=`.
- [x] 4.3 Add inclusive range tests, including boundaries.
- [x] 4.4 Add known-zero tests (`ondisk:0B`, `ondisk:<=0B`, a range containing zero).
- [x] 4.5 Add UNKNOWN tests proving no numeric `ondisk:` predicate matches unknown, including an unknown candidate whose raw allocated byte member is deliberately nonzero.
- [x] 4.6 Add a fixture where logical and allocated sizes differ and prove `size:` and `ondisk:` select different membership.
- [x] 4.7 Add implicit-AND tests (`ext:` + `ondisk:`, free text + `ondisk:`) and scope-preservation tests.
- [x] 4.8 Add malformed/overflow/unit tests and prove invalid values are reported through `invalidFilters` while `ondisk` itself is not reported in `unrecognizedKeys`.
- [x] 4.9 Prove owning and view predicate paths return identical membership for the same fixture.

## 5. Engine/degraded integration

- [x] 5.1 Add/extend projection-backed indexed Search tests with known nonzero, known zero, and unknown allocated metadata; verify `ondisk:` membership comes directly from projection fields.
- [x] 5.2 Prove the accelerated view path remains eligible and does not add eager path materialization for `ondisk:` queries.
- [x] 5.3 Verify cancellation remains chunk-bounded and superseded queries discard partial results exactly as before.
- [x] 5.4 Verify degraded/snapshot candidates remain allocated-unknown and therefore do not match numeric `ondisk:` predicates.
- [x] 5.5 Verify no database lookup, filesystem probe, service request, protocol expansion, or second candidate pass is introduced.

## 6. Search UI and live integration

- [x] 6.1 Ensure the Search UI treats `ondisk` as a recognized structured key and preserves the existing feedback behavior for truly unrecognized/invalid keys.
- [x] 6.2 Add or extend a focused live Search scenario using a deterministic degraded fixture: normal/free-text or `size:` query finds the fixture; `ondisk:>=0B` returns no fabricated UNKNOWN matches; returning to the normal query restores the results.
- [x] 6.3 If the current Search UI exposes an unrecognized-filter indication, assert `ondisk` does not trigger it. Do not add a new UI solely for this test unless a real UX defect is discovered.
- [x] 6.4 Preserve Search result UIA discovery, query replacement, structured-filter scenario, and deep-result navigation; zero unexpected FAIL.
- [x] 6.5 Run the focused scenario repeatedly (target 10/10 if it has an isolated lifecycle), then run the full `windows-ui-automation-validation` capability.
- [x] 6.6 Preserve legitimate independent SKIPs; never convert a new Search regression to SKIP.

## 7. Performance/regression evidence

- [x] 7.1 Compare representative `size:` and `ondisk:` metadata-filter runs using the existing search benchmark/diagnostic path or a focused equivalent.
- [x] 7.2 Record candidate count, result count, path-needed decisions/path lookups, and total/filter time; demonstrate no material algorithmic regression.
- [x] 7.3 Confirm `ondisk:` does not change result caps, wire response width, query request layout, or protocol IDs/versions.
- [x] 7.4 Re-run existing logical `size:` tests and Size/Size-on-Disk sorting tests; all remain unchanged.

## 8. Evidence, validation, and handoff

- [x] 8.1 Add `evidence/implementation-evidence.md` with parser/predicate matrix, known/zero/unknown truth table, owning-vs-view equivalence, degraded live evidence, performance evidence, and final validation results.
- [x] 8.2 Update `docs/agent/WORKSTATE.md` truthfully with this change and the reconciled prior closure state.
- [x] 8.3 Run focused query/search/engine/UI tests.
- [x] 8.4 Run Debug configure/build + all CTest; PASS with none Not Run.
- [x] 8.5 Run release-test configure/build + all CTest; PASS.
- [x] 8.6 Run analyze `/W4 /WX`; warning-clean.
- [x] 8.7 Run `pwsh ./verify/verify.ps1 build` and `doctor`.
- [x] 8.8 Run source-marker, diagnostic-policy, doc-drift, strict OpenSpec validation, and `git diff --check`.
- [x] 8.9 Review full diff for scope; do not stage generated `verify/runs`, build artifacts, DBs, logs, screenshots, or fixture files.
- [x] 8.10 Commit and push validated changes to `main`; verify `HEAD == origin/main` and clean tracked worktree.
- [x] 8.11 Query hosted CI for the exact final pushed SHA when the environment can surface it. Report the exact observed result; do not invent a conclusion or create a recursive commit solely to store it.

## Completion gate

`COMPLETE` requires:

- `ondisk:` registered and using the same numeric grammar as `size:`;
- known nonzero/known zero/comparison/range semantics correct;
- UNKNOWN matches no numeric `ondisk:` predicate and is never fabricated;
- `size:` remains logical and unchanged;
- owning and projection-view evaluation are equivalent;
- no eager path materialization or new DB/FS/service/IPC/protocol work;
- combined AND/scope/cancellation behavior remains sound;
- focused live Search integration and full UIA have zero unexpected FAIL;
- prior WORKSTATE closure truth is reconciled;
- all native/build/spec/policy gates pass;
- final validated state is pushed to `main`, with hosted CI status reported exactly if retrievable.