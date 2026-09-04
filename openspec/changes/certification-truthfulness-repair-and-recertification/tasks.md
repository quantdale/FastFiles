# Tasks: Certification Truthfulness Repair and Recertification

## 1. Forensic truth repair and defect tracking
- [x] 1.1 Complete repository forensics and confirm status of defects CT-001 through CT-010 in `docs/agent/CERTIFICATION_TRUTHFULNESS_DEFECT_LEDGER_2026-09-04.md`
- [x] 1.2 Create OpenSpec change `certification-truthfulness-repair-and-recertification` with proposal, design, and tasks
- [x] 1.3 Validate OpenSpec strict compliance across all repository changes

## 2. Long-run runner hardening
- [x] 2.1 Implement CTest target discovery (`--show-only=json-v1`) in `verify/certification/Invoke-LongRunCertification.ps1` enforcing exactly one matching test
- [x] 2.2 Implement per-iteration CTest execution proof verification (verifying output indicates test started and completed)
- [x] 2.3 Implement direct executable execution with executable SHA-256 hash logging and duration tracking
- [x] 2.4 Add fail-closed unit tests in `Test-LongRunCertification.ps1` proving zero-test targets fail closed as `INVALID_TARGET`

## 3. UIA long-run adapter and external frontier audit
- [x] 3.1 Implement `verify/certification/Invoke-UiaLongRunCertification.ps1` supporting semantic readiness, graceful shutdown, and bounded iterations
- [x] 3.2 Implement representative application lifecycle runner (`launch/ready/shutdown` x100) and rapid-input runner (x50)
- [x] 3.3 Separate vision provider certification (`Invoke-VisionProviderCertification.ps1`) from environment provider certification (`Invoke-EnvironmentProviderCertification.ps1`)
- [x] 3.4 Re-audit all 25 external frontier tasks in `verify/certification/external-frontier.json`, eliminating nonexistent `ffuia_smoke_tests` references and semantic mismatches
- [x] 3.5 Add automated command executability and lane semantic tests in `Test-ExternalFrontierInventory.ps1`

## 4. Resource soak engine and schema redesign
- [x] 4.1 Redesign `verify/certification/Invoke-ResourceSoak.ps1` around the 7-phase lifecycle (Launch, Ready Wait, Warmup, Steady-State Baseline, Measured Soak, Shutdown, Residue Check)
- [x] 4.2 Eliminate fake simulated UI activity; implement real UIA transitions with verified postconditions or explicit `-IdleOnly` mode
- [x] 4.3 Implement steady-state resource stability metrics (linear slope, first-half vs second-half medians, peak drift bounds)
- [x] 4.4 Implement engine process detection and PID tracking, and verify graceful window close vs forced kill
- [x] 4.5 Update `verify/certification/schemas/resource-soak-result.schema.json` to represent phases, steady-state baselines, and stability disposition
- [x] 4.6 Add adversarial soak tests in `Test-ResourceSoak.ps1` proving failure on runaway resources, crashes, and ungraceful exits

## 5. Artifact hash integrity, path sanitization, and consistency gates
- [x] 5.1 Fix artifact self-hashing defect in long-run and soak runners; implement authoritative manifest hashing
- [x] 5.2 Sanitize absolute development file paths to logical repository-relative paths across all output artifacts
- [x] 5.3 Implement automated report-vs-artifact consistency validator and JSON-to-markdown summary generator

## 6. Clean implementation commit and exact-tree recertification
- [x] 6.1 Run all certification test suites and verify 100% pass on working tree
- [x] 6.2 Commit all certification tooling repairs to establish a clean implementation commit
- [x] 6.3 Rebuild test executables from the exact implementation commit and record executable hashes
- [x] 6.4 Execute authoritative exact-tree long-run stress suite with `-RequireExactTree` (IPC handshake x100, framing x100, UiServer x25, concurrency x20, dialog focus x50, lifecycle x100, rapid-input x50)
- [x] 6.5 Execute authoritative exact-tree 600-second real semantic resource soak on clean tree
- [x] 6.6 Generate authoritative evidence manifest and markdown summaries from retained JSON artifacts

## 7. OpenSpec and lifecycle convergence
- [x] 7.1 Commit authoritative evidence artifacts under `openspec/changes/certification-truthfulness-repair-and-recertification/evidence/`
- [x] 7.2 Update OpenSpec tasks and mark all completed tasks `[x]`
- [x] 7.3 Update `openspec/lifecycle.json` with authoritative evidence paths and implementation commit binding
- [x] 7.4 Run full repository validation ladder (Debug CTest, release-test CTest, Analyze, ASAN, Visual contracts, diagnostic policy, OpenSpec strict)
- [x] 7.5 Run `verify/qualification/Invoke-ExactTreeQualification.ps1` and verify clean-tree PASS
## 8. Hosted CI certification and final binding
- [x] 8.1 Push clean certified commit to `main` without force push
- [x] 8.2 Monitor hosted GitHub Actions CI run to completion and verify 6/6 jobs SUCCESS
- [x] 8.3 Record final hosted run ID and SHA binding in `openspec/lifecycle.json` and documentation
- [x] 8.4 Produce comprehensive final campaign closure report
