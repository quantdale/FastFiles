# Tasks: External Certification Readiness and Evidence Convergence

## 1. OpenSpec authority and external frontier inventory
- [x] 1.1 Create OpenSpec change `external-certification-readiness-and-evidence-convergence` with proposal, design, and tasks
- [x] 1.2 Inventory all 25 unchecked external tasks into `verify/certification/external-frontier.json`
- [x] 1.3 Validate `external-frontier.json` against `verify/certification/schemas/external-frontier.schema.json`

## 2. Durable stress and soak schemas and runners
- [x] 2.1 Implement `verify/certification/schemas/long-run-result.schema.json`, `resource-soak-result.schema.json`, and `certification-result.schema.json`
- [x] 2.2 Implement `verify/certification/Invoke-LongRunCertification.ps1` (Git provenance, per-iteration timeouts, failure capture, timing statistics)
- [x] 2.3 Implement `verify/certification/Invoke-ResourceSoak.ps1` (process metrics, handles, threads, USER/GDI objects, periodic sampling, trend rates)
- [x] 2.4 Add unit and fail-closed tests for long-run and soak runners

## 3. Re-execution of representative stress and soak suite
- [x] 3.1 Execute representative IPC and lifecycle stress suite (`ffengine_client_handshake_tests` ×100, `ffipc_framing_tests` ×100, `ffengine_ui_server_lifecycle_tests` ×25)
- [x] 3.2 Execute representative application lifecycle and rapid stress suite (launch/ready/shutdown ×100, rapid-input ×50)
- [x] 3.3 Execute 300-second resource soak test on diagnostic FastFiles build
- [x] 3.4 Retain sanitized machine-readable evidence under `openspec/changes/external-certification-readiness-and-evidence-convergence/evidence/`

## 4. Clean-host certification package
- [x] 4.1 Implement `verify/certification/Invoke-CleanHostCertification.ps1` (default audit mode, conflict detection, candidate verification, optional install/uninstall, zero-residue check)
- [x] 4.2 Add unit and fail-closed tests for clean-host certification (`Test-CleanHostCertification.ps1`)

## 5. Privileged NTFS read-only certification package
- [x] 5.1 Implement `verify/certification/Invoke-PrivilegedNtfsCertification.ps1` (elevation check, NTFS check, raw MFT and USN read-only access, isolated campaign fixture validation)
- [x] 5.2 Add unit and fail-closed tests for privileged NTFS certification (`Test-PrivilegedNtfsCertification.ps1`)

## 6. Production signing certification package
- [x] 6.1 Implement `verify/certification/Invoke-SigningCertification.ps1` (Authenticode verification, thumbprint and pin check, peer-image hash check, unsigned rejection)
- [x] 6.2 Add unit and fail-closed tests for signing certification (`Test-SigningCertification.ps1`)

## 7. Real vision provider certification package
- [x] 7.1 Implement `verify/certification/Invoke-ProviderCertification.ps1` (provider detection, secret redaction, schema validation, FakeCritic fail-closed rejection)
- [x] 7.2 Add unit and fail-closed tests for provider certification (`Test-ProviderCertification.ps1`)

## 8. Top-level external certification orchestrator
- [x] 8.1 Implement `verify/certification/Invoke-ExternalCertification.ps1` (capability matrix discovery, lane dispatch, no success by skip)
- [x] 8.2 Add orchestrator capability detection and execution tests

## 9. Evidence import and staleness engine
- [x] 9.1 Implement `verify/certification/Import-CertificationEvidence.ps1` (schema validation, tree binding, lane-specific staleness rules)
- [x] 9.2 Add unit tests for evidence import and staleness detection

## 10. External certification runbook and documentation
- [x] 10.1 Document copy-pasteable operator workflows in `docs/certification/EXTERNAL_CERTIFICATION_RUNBOOK.md`
- [x] 10.2 Update project status reporting to link external tasks directly to certification commands and schemas

## 11. Validation and hosted CI certification
- [x] 11.1 Run full local validation ladder (Debug CTest, release-test CTest, Analyze, ASAN, Visual contracts, OpenSpec strict)
- [x] 11.2 Pass exact-tree qualification and register change in `openspec/lifecycle.json`
- [x] 11.3 Push and certify final commit with 6/6 SUCCESS on GitHub Actions
