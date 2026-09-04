## 1. Status and frontier generation

- [x] 1.1 Add schema-versioned project-status model and pure task/state classifiers for PASS, FAIL, SKIPPED, BLOCKED_ENVIRONMENT, and REQUIRED-BUT-UNAVAILABLE.
- [x] 1.2 Implement Git/CTest/OpenSpec/capability/release evidence collection with safe path sanitization and explicit last-result discovery.
- [x] 1.3 Implement JSON source generation and Markdown projection with release-frontier categories and exact next actions.
- [x] 1.4 Add `verify.ps1 status`, document the generator/output contract, and keep `progress.md` free of stale dynamic counts.
- [x] 1.5 Add status fixtures for clean/dirty worktrees, absent results, category classification, stale-count detection, and sanitized output.

## 2. Provider readiness and lifecycle contracts

- [x] 2.1 Extend generic provider schema/core validation with preflight entry points, stable reason codes, source metadata, and explicit cleanup outcomes.
- [x] 2.2 Historical vendor-specific provider preflight/source-validation work is retained as implementation history only. Under the 2026-09-04 owner directive, provider-specific VM execution/testing is not a FastFiles local completion requirement.
- [x] 2.3 Add reproducible secret-free bootstrap/plan generation that records `not-executed` until real external evidence is supplied; never materialize credentials in repository evidence.
- [x] 2.4 Add a fake provider and contract runner covering success, lifecycle failure, timeout, cancellation, restore, interrupted resume, corrupt descriptor, unsupported version, dependency failure, and repeated cleanup.
- [x] 2.5 Document generic provider readiness and external-operator boundaries without making any vendor-specific VM run a completion gate.

## 3. Release qualification and CI

- [x] 3.1 Add bounded CI jobs for strict OpenSpec, source/diagnostic/status drift, portable trust tests, generic provider contracts, release-candidate dry run, and determinism checks.
- [x] 3.2 Emit safe structured/JUnit artifacts and named failure diagnostics; keep production signer/pins and privileged trust-store tests opt-in.
- [x] 3.3 Add release-candidate regression tests for checksum/provenance/release-note/dependency reproducibility, stale version metadata, malformed pins, and cleanup after failure.
- [x] 3.4 Cross-check status and release-readiness composition so external requirements remain blocked rather than inferred PASS.

## 4. Audit and evidence

- [x] 4.1 Create the Phase-8 audit ledger with finding IDs, severity, reproduction, disposition, implementation commit, regression coverage, and remaining prerequisite.
- [x] 4.2 Audit tracked markers, stale claims, provider descriptors, CI, release scripts, and recent high-risk seams; fix locally reproducible P0/P1/P2 issues with regression coverage.
- [x] 4.3 Generate the authoritative release-readiness/frontier report and reconcile only tasks whose acceptance evidence exists.
- [x] 4.4 Run the local validation ladder and record exact external blockers without modifying trust stores or inventing guest/signing evidence.
- [x] 4.5 Reconcile with the remote, commit coherent waves, push without force, and inspect hosted CI for pushed SHAs.

## 5. External evidence gates

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is excluded from FastFiles completion. Historical provider-related task IDs below remain for lifecycle compatibility but do not create local work.

- [ ] 5.1 Historical snapshot-restorable vendor-specific VM-provider validation — **OWNER-EXCLUDED; NOT A LOCAL COMPLETION GATE**.
- [ ] 5.2 Execute signed privileged install/runtime and real MFT/USN validation with authorized production signer/pins on a clean supported host.
- [ ] 5.3 Execute remaining physical DPI/monitor/High Contrast/interactive evidence where required. Any historical cross-provider VM-equivalence component is **OWNER-EXCLUDED**.
