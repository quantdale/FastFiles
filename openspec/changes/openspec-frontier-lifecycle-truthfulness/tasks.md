# Tasks — openspec-frontier-lifecycle-truthfulness

## 1. Baseline and lifecycle model

- [x] 1.1 Record the live unchecked-task inventory, false `localOpen` examples, and historical Git/CI evidence in the campaign evidence files. — baseline and historical ledgers recorded under `evidence/` from `b0ed580`.
- [x] 1.2 Add the schema-validated `openspec/lifecycle.json` model and lifecycle schema with active, completed, superseded, external-evidence-only, and ambiguous states. — `openspec/lifecycle.json`, `verify/schemas/openspec-lifecycle.schema.json`, and deterministic model validation are live.
- [x] 1.3 Reconcile only evidenced historical delivery tasks in the affected ledgers; leave external/provider/signing/interactive/clean-host tasks open. — stale delivery tasks are annotated/checked from later Git/CI evidence; genuine external gates remain unchecked.

## 2. Status and release projection

- [x] 2.1 Make OpenSpec collection lifecycle-aware, fail closed on model errors, preserve raw tasks, and expose lifecycle/task provenance. — `Get-ProjectStatusLifecycleModel`/`Get-ProjectStatusOpenSpecFrontier` retain raw tasks and route model failures to ambiguity.
- [x] 2.2 Replace local-only fallback promotion with explicit categories/dispositions, mixed and administrative buckets, and deterministic ordering. — explicit metadata, `mixedOpen`, `administrativeBlocked`, historical/superseded buckets, and stable ordering are implemented.
- [x] 2.3 Update project-status/release-readiness schemas, Markdown, command exit behavior, and all affected consumers. — schema v2, status/release projections, `verify.ps1 status`, README, CODE_INDEX, and AUTONOMOUS guidance converge.

## 3. Deterministic regression coverage

- [x] 3.1 Add fixture coverage for active local, historical external, stale administrative, superseded, malformed metadata, explicit-vs-prose classification, mixed tasks, external buckets, stable ordering, and path redaction. — `Test-ProjectStatus.ps1` passes the complete deterministic fixture matrix.
- [x] 3.2 Validate generated project-status and release-readiness JSON against their schemas and prove lifecycle filtering cannot infer production readiness. — focused status tests pass both schema checks and the non-production release assertion.

## 4. Repository-wide convergence

- [x] 4.1 Audit status/release/planner documentation and consumers for raw versus active task-count assumptions; update guidance without weakening existing security or external gates. — `verify.ps1`, status/release scripts, `verify/README.md`, `CODE_INDEX.md`, and `AUTONOMOUS.md` now describe raw versus active lifecycle state.
- [x] 4.2 Run focused tests, strict OpenSpec, policy/doc-drift checks, verifier doctor/build, Debug/release-test/Analyze/ASAN applicable gates, and `git diff --check`. — focused status 1/1; Debug/release-test CTest 83/83; Analyze clean; ASAN protocol/IPC 3/3; verifier build 9/9; source/diagnostic/doc-drift/policy checks pass; strict OpenSpec 70/70.
- [ ] 4.3 Update evidence, progress, workstate, and this task ledger from final Git/CI reality; mark the planner prompt complete only at final clean handoff.
