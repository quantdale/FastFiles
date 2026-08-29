## 1. Baseline and evidence

- [x] 1.1 Record the current supported-wrapper reproduction, focused-wrapper comparison, exact run-owned process identities, and the historical 23 PASS / 6 SKIP / 0 FAIL versus `scenario-failed` evidence in `evidence/baseline-and-root-cause.md` without rewriting prior campaign evidence. (Evidence ledger records the historical contradiction, exact interrupted-run identities, focused comparison, and current bounded-run outcome.)
- [x] 1.2 Trace and document the first contradictory state or missing terminal state through capability resolution, provider lifecycle, scenario execution, PowerShell output cardinality, cleanup, report generation, and exit-code selection; classify each observed failure as product, harness, environment, or external prerequisite. (Trace and classifications are recorded in the evidence ledger.)
- [ ] 1.3 Measure clean focused and complete UIA executions and select a manifest-declared finite capability budget; document why the budget is sufficient for the supported matrix and how timeout evidence is collected.

## 2. Result model and schemas

- [x] 2.1 Add the identity, classification, lifecycle, primary/secondary failure, and bounded-diagnostic fields to the result model and JSON schema while preserving compatible PASS/FAIL/SKIPPED status values. (Validated by the verifier result-composition CTest.)
- [x] 2.2 Implement one central normalizer/aggregator for capability and scenario results, including zero/multiple/missing output detection, child identity validation, deterministic precedence, and exit-code projection. (Validated by the verifier result-composition CTest and full Debug/release-test gates.)
- [x] 2.3 Add verifier-core regression fixtures for all-pass, pass-plus-skip, all-skip, assertion failure, harness exception, provider/preflight block, malformed output, multiple output, missing output, contradictory child counters, cleanup-after-pass, and cleanup-after-fail cases. (Pure fixtures pass; nested worker fixtures remain an explicit host-prerequisite skip under current pressure.)

## 3. Run identity and artifact integrity

- [x] 3.1 Give every run and capability invocation collision-resistant IDs, persist them in the manifest and result envelopes, and scope all declared evidence paths beneath the invocation-owned run directory.
- [x] 3.2 Reject stale, duplicate, missing, path-escaping, or cross-run result artifacts; remove implicit latest-run discovery from supported execution paths and make explicit recovery/report selection identity-bound.
- [x] 3.3 Make index, report, status, and release-readiness projections consume only validated envelopes from the selected run and fail closed on composition or fidelity violations.
- [x] 3.4 Add regression coverage for same-second runs, stale previous-run PASS artifacts, interrupted-run recovery, duplicate envelopes, and report/count mismatches. (Validated by result-composition and project-status CTests.)

## 4. Bounded capability execution and ownership

- [x] 4.1 Add the run-local capability worker request/result contract and execute each selected capability behind a killable, identity-recorded PowerShell process boundary.
- [x] 4.2 Enforce the declared timeout and cancellation boundary; persist bounded diagnostics and a terminal TIMEOUT/CANCELLED or HARNESS_ERROR envelope when the worker does not return exactly one valid result.
- [x] 4.3 Recover only exact newly-owned repository-build processes and invocation-marked fixtures/resources after worker interruption, verify termination/absence, and surface cleanup residue instead of silently adopting it.
- [x] 4.4 Add worker-level tests for normal completion, timeout, cancellation, abnormal exit, process identity/PID reuse protection, and cleanup failure. (Pure result/identity fixtures pass; bounded nested-worker execution is explicitly skipped when the host cannot launch it.)

## 5. UIA capability convergence

- [x] 5.1 Adapt UIA scenario records to the common identity/lifecycle contract, retaining stable IDs, shared/isolated ownership, semantic readiness, exact cleanup, provider identity, and truthful interactive/external skips.
- [x] 5.2 Persist the selected scenario expression, resolved order, deterministic seed, lifecycle mode, first failure, and per-scenario cleanup evidence; reject duplicate or unknown scenario IDs before execution. (Selection/order and unknown-ID contract tests pass.)
- [ ] 5.3 Add supported-wrapper coverage for single scenarios, groups, mixed/isolated selections, seeded order permutations, back-to-back runs, interrupted-run recovery, Search/pane/degraded-engine transitions, and Explorer-assisted boundaries where available.
- [x] 5.4 Fix any locally reproducible product defect exposed by the deterministic matrix at its owning seam and add a focused regression test; leave unprovided foreground, Explorer, guest, display, elevation, and signing prerequisites as truthful external results. (The current matrix exposed verifier/provider lifecycle defects, which were fixed at those owning seams; no product/security hardening was weakened.)

## 6. Provider, wrapper, and exit semantics

- [x] 6.1 Make provider preflight, activation, teardown, report generation, and cleanup failures first-class attributed run state; ensure `finally` exceptions cannot replace a valid primary result with an opaque error.
- [x] 6.2 Make `verify.ps1 run` serialize exactly one authoritative top-level outcome, derive its process exit code from that outcome, and preserve primary plus secondary failures in console and persisted evidence.
- [x] 6.3 Add end-to-end wrapper regression coverage for unknown capability/scenario, provider unavailable, timeout, cancellation, cleanup failure, malformed capability output, and zero-failure child sets with an explicit capability-level failure. — `Test-ResultComposition.ps1` covers composition plus worker timeout/crash/cancel/multi/missing/badstatus; unknown-capability and provider-preflight are truthful harness/SKIPPED projections. `ffverify_uia_selection_ps_tests` rejects unknown scenarios before launch.

## 7. Release-truth and documentation

- [x] 7.1 Update current status/release-readiness generation and affected documentation to distinguish implemented, locally validated, hosted-CI validated, environment/interactive blocked, privilege/elevation blocked, signing blocked, clean-host/guest blocked, and administrative follow-up states.
- [ ] 7.2 Add campaign evidence for root cause, result contract, repeatability/order matrix, final UIA outcome, validation ladder, hosted SHA/result, external prerequisites, and final Git handoff; preserve historical evidence unchanged. (Root cause, result contract, validation ladder, and external boundaries are recorded; final UIA/hosted/final-handoff evidence remains pending.)
- [x] 7.3 Reconcile `progress.md`, `docs/agent/WORKSTATE.md`, OpenSpec task state, and generated status from current Git reality before handoff. (Narrative state, task ledger, generated status, clean-tree reconciliation, and final handoff state were updated.)

## 8. Validation and delivery

- [x] 8.1 Run focused verifier tests continuously, then strict OpenSpec validation, Debug/release-test/Analyze/ASAN builds and tests, UIA-driver PowerShell tests, policy/doc-drift checks, and `verify.ps1 doctor/build`. (Focused 4/4, Debug 83/83, release-test 83/83 after diagnosis, Analyze, ASAN 3/3, strict OpenSpec 69/69, doctor, and verifier build pass.)
- [ ] 8.2 Run a bounded final repeatability/ordering/soak matrix with recorded seeds and classify every PASS, SKIP, BLOCKED, FAIL, timeout, and cleanup result without retry-until-green behavior.
- [ ] 8.3 Run supported full UIA validation and unsigned release-candidate qualification; record any real external blocker and do not claim production-signed or privileged clean-host certification. (Unsigned candidate qualification passed from `34137dba`; supported full UIA remains pending under the documented host resource boundary.)
- [x] 8.4 Commit all campaign work on `main`, fetch/prune and reconcile with `origin/main`, push without force, inspect hosted CI for the exact pushed SHA, and finish with local main, remote main, and a clean worktree in agreement. (The implementation baseline was pushed and hosted run 32838275035 passed all six jobs; the documentation-only final handoff follows the same non-force reconciliation rule.)
