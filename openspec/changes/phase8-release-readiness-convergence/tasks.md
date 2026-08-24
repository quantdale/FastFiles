## 1. Status and frontier generation

- [x] 1.1 Add the schema-versioned project-status model and pure task/state classifiers for PASS, FAIL, SKIPPED, BLOCKED_ENVIRONMENT, and REQUIRED-BUT-UNAVAILABLE. Evidence: `verify/core/ProjectStatus.psm1`, `verify/schemas/project-status.schema.json`, and `tests/status`/PowerShell fixtures.
- [x] 1.2 Implement Git/CTest/OpenSpec/capability/release evidence collection with safe path sanitization and explicit last-result discovery. Evidence: `Get-ProjectStatusCtestEvidence` reads configured CTest discovery and `LastTest.log` without substituting maintained counts.
- [x] 1.3 Implement JSON source generation and Markdown projection with the five release-frontier categories and exact next actions. Evidence: `verify/status/Get-ProjectStatus.ps1` and `New-ReleaseReadinessReport.ps1`; local-open work is kept separate from locally certified evidence.
- [x] 1.4 Add `verify.ps1 status` and document the generator/output contract; make `progress.md` a thin pointer with no dynamic counts. Evidence: `verify/verify.ps1`, `verify/README.md`, `progress.md`, and drift coverage.
- [x] 1.5 Add status fixture tests for clean/dirty worktrees, absent results, open-task category classification, stale-count detection, and sanitized output. Evidence: `verify/status/tests/Test-ProjectStatus.ps1` passes, including project-status and release-frontier schema round trips.

## 2. Provider readiness and lifecycle contracts

- [x] 2.1 Extend provider schema/core validation with preflight entry points, stable reason-code vocabulary, source metadata, and explicit cleanup outcomes. Evidence: `verify/schemas/environment-provider.schema.json`, `verify/core/Providers.psm1`, and provider contract tests.
- [x] 2.2 Harden Hyper-V preflight and user-supplied ISO/VHDX/template validation; distinguish absent media, unusable Windows image, guest reachability, credentials, and resource failures. Evidence: read-only Windows hive inspection, stable reason codes, and `verify.ps1 doctor`/Hyper-V preflight diagnostics; no arbitrary VHDX is treated as a guest.
- [x] 2.3 Add reproducible secret-free bootstrap/unattended-plan generation that records `not-executed` until a real guest supplies evidence. Evidence: `Invoke-HyperVProviderBootstrapPlan` emits a deterministic plan with external secret inputs and `executed=false`; it never mounts media or materializes credentials.
- [x] 2.4 Add a fake provider and contract runner covering success, every lifecycle failure, timeout, cancellation, restore, interrupted resume, corrupt descriptor, unsupported version, dependency failure, and repeated cleanup. Evidence: `verify/providers/tests/Test-ProviderContract.ps1` passes all named lifecycle cases.
- [x] 2.5 Add provider readiness documentation and a deterministic next command for an authorized operator supplying a real Windows guest. Evidence: `verify/README.md`, `verify/providers/hyperv/provider.json`, and the documented `verify.ps1 run -Provider hyperv -ProviderSource <authorized-image>` path.

## 3. Release qualification and CI

- [x] 3.1 Add bounded CI jobs for strict OpenSpec, source/diagnostic/status drift, portable trust tests, provider contracts, release-candidate dry run, and determinism checks. Evidence: `.github/workflows/ci.yml` adds named hygiene and unsigned-release-qualification jobs; portable trust tests remain in the existing Debug/release-test matrix.
- [x] 3.2 Emit safe structured/JUnit artifacts and named failure diagnostics; keep production signer/pins and privileged trust-store tests opt-in. Evidence: existing run-tree JUnit/JSON reporting plus CI-uploaded sanitized status/release JSON; no signing material or screenshots are uploaded.
- [x] 3.3 Add release-candidate regression tests for checksum/provenance/release-note/dependency inventory reproducibility, stale version metadata, malformed pins, and cleanup after failure. Evidence: `Test-ReleaseCandidate.ps1` passed twice with identical archive hashes and fail-closed production-candidate rejection; existing `Test-ReleaseTooling.ps1` covers version, pin, malformed-input, and cleanup cases.
- [x] 3.4 Cross-check status and release-readiness composition fields so external requirements remain blocked rather than inferred PASS. Evidence: status fixture composition validates the frontier schema and explicitly classifies a PASS unsigned fixture as `NON-PRODUCTION-OR-BLOCKED`; production readiness requires the production artifact gate.

## 4. Audit and evidence

- [ ] 4.1 Create the Phase-8 audit ledger with finding IDs, severity, reproduction, disposition, implementation commit, regression coverage, and remaining prerequisite.
- [ ] 4.2 Audit tracked markers, stale claims, provider descriptors, CI, release scripts, and recent high-risk seams; fix every locally reproducible P0/P1/P2 issue with a regression test.
- [ ] 4.3 Generate the authoritative release-readiness/frontier report and reconcile only OpenSpec tasks whose acceptance evidence exists.
- [ ] 4.4 Run the complete local validation ladder and record exact external blockers without modifying trust stores or inventing guest/signing evidence.
- [ ] 4.5 Reconcile with the remote, commit coherent waves, push without force when authenticated, and inspect hosted CI for the pushed SHA. — external when GitHub authentication/network is unavailable.

## 5. External evidence gates

- [ ] 5.1 Validate a real snapshot-restorable Windows provider with an authorized OS image, reachable secure guest channel, toolchain, and clean checkpoint.
- [ ] 5.2 Execute the signed privileged install/runtime and real MFT/USN validation with authorized production signer/pins on a clean supported host.
- [ ] 5.3 Execute the remaining physical DPI/monitor/High Contrast/interactive matrix and cross-provider equivalence; do not infer these from headless tests.
