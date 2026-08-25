# Tasks — openspec-lifecycle-evidence-integrity-and-semantic-convergence

## 1. Baseline and contract

- [x] 1.1 Record the pulled baseline, raw unchecked inventory, current lifecycle entries, evidence meanings, and consumer impact map in the campaign evidence. (Evidence: `evidence/baseline.md`, `evidence/current-manifest-audit.md`.)
- [x] 1.2 Define and document the schema-v2 evidence reference model and semantic invariant matrix. (Evidence: `evidence/evidence-reference-model.md`, `evidence/semantic-invariant-matrix.md`.)

## 2. Typed evidence resolution

- [x] 2.1 Add schema-v2 typed evidence objects and reject legacy string evidence. (Evidence: `verify/schemas/openspec-lifecycle.schema.json`, `verify/core/ProjectStatus.psm1`, focused fixture coverage.)
- [x] 2.2 Resolve repository paths safely, full reachable Git commits offline, and durable hosted-run bindings without network access. (Evidence: offline resolver seams and path/Git/hosted fixtures in `verify/status/tests/Test-ProjectStatus.ps1`.)
- [x] 2.3 Project normalized authority/source metadata without absolute host paths. (Evidence: lifecycle projections, status/readiness schemas, and redaction assertions.)

## 3. Semantic lifecycle validation

- [x] 3.1 Add separate structural, evidence, and semantic lifecycle diagnostics. (Evidence: `ProjectStatus.psm1` model and projected status/readiness fields.)
- [x] 3.2 Enforce authoritative state transitions, supersession graph integrity, and task disposition/category/status/evidence invariants. (Evidence: semantic fixture matrix covering state, cycles, contradictions, and status/category rules.)
- [x] 3.3 Preserve raw unchecked tasks and fail closed on integrity errors across project status and release readiness. (Evidence: malformed fixture remains ambiguous and release fail-closed assertion.)

## 4. Migration and regression coverage

- [x] 4.1 Migrate every live lifecycle entry to typed, resolvable evidence and preserve genuine external gates. (Evidence: `openspec/lifecycle.json`, `evidence/current-manifest-audit.md`.)
- [x] 4.2 Add deterministic fixture coverage for evidence resolution, path safety, Git/hosted bindings, cycles, contradictions, drift, redaction, ordering, and release fail-closed behavior. (Evidence: `verify/status/tests/Test-ProjectStatus.ps1` passes all assertions offline.)
- [x] 4.3 Audit and update every lifecycle/status/release/planner consumer and documentation for the new contract. (Evidence: `ProjectStatus.psm1`, readiness projection, schemas, `.github/workflows/ci.yml`, `AUTONOMOUS.md`, `verify/README.md`, and `CODE_INDEX.md`.)

## 5. Validation and handoff

- [x] 5.1 Run focused status tests, schema checks, strict OpenSpec and policy/doc-drift gates. (Evidence: focused status suite PASS; strict OpenSpec 71/71; diagnostic policy PASS; composition doc-drift gate PASS.)
- [x] 5.2 Run the supported Debug, release-test, Analyze, ASAN/applicable verifier gates and review the complete diff. (Evidence: official build gate 9/9 PASS; Debug/release-test CTest PASS; Analyze warning-clean; protocol robustness 3/3 PASS; `git diff --check` PASS. Release composition also recorded unrelated runtime harness and external unsigned/clean-host blockers.)
- [x] 5.3 Update evidence, progress, workstate, lifecycle state, and this ledger from final Git/CI reality; commit, push, reconcile, and inspect exact hosted CI. (Evidence: corrected hosted run `32865014793` completed successfully for exact implementation SHA `09bac46a5371ecaad58b7c38ab044deb2c5d8163`; final handoff Git/CI state is derived live and kept non-self-referential.)
