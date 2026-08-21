## 1. Disposable dogfood fixtures

- [x] 1.1 Add a deterministic PowerShell fixture generator with explicit disposable-root validation, manifest output, and small/medium/large profiles. (`verify/dogfood/New-DogfoodFixture.ps1`)
- [x] 1.2 Cover Unicode, emoji, long/extended paths, hidden/read-only/reparse-safe cases, mixed content types, timestamps, sizes, and duplicate basenames in the fixture manifest. (`verify/dogfood/New-DogfoodFixture.ps1`; reparse creation remains explicit best-effort.)
- [x] 1.3 Add an independent manifest snapshot/compare helper for expected file-operation truth and a safe reset command that refuses unmarked roots. (`verify/dogfood/{DogfoodCommon,Write-DogfoodSnapshot,Compare-DogfoodFixture,Reset-DogfoodFixture}`)
- [x] 1.4 Add focused tests or script checks for fixture determinism, root confinement, reset safety, and bounded item generation. (`verify/dogfood/tests/Test-DogfoodFixture.ps1`, PASS)

## 2. End-user acceptance and dogfood evidence

- [x] 2.1 Create `docs/quality/USER_ACCEPTANCE.md` with scenario IDs, preconditions, steps, expected behavior, automation status, environment requirements, and evidence fields.
- [x] 2.2 Create the distinct `codebase-audit/sessions/2026-08-21-dogfood/` ledger with severity/reproduction/root-cause/fix/regression/status fields and explicit external-blocker handling.
- [ ] 2.3 Add stable semantic/UIA acceptance automation for launch, Home/degraded browsing, navigation/tabs/dual-pane, search, settings persistence, preview/storage, and restart/session restore where the current harness can support it.
- [ ] 2.4 Add disposable-fixture file-operation acceptance for copy, move, rename, conflict, cancellation, delete/restore/undo, and failure injection, comparing results independently.
- [ ] 2.5 Execute bounded first-run, persistence/corruption, degraded-mode, keyboard, accessibility, and failure-recovery scenarios; record actual evidence and classify only observed findings.

## 3. Measurement and compatibility evidence

- [ ] 3.1 Add a bounded dogfood measurement command or script for cold/warm launch, folder-open, search, preview/storage, and large-directory profiles with build/environment identity.
- [ ] 3.2 Add a bounded soak loop with periodic working-set, private-bytes, handle, thread, CPU, and process-state samples for UI and engine.
- [ ] 3.3 Record baseline figures for feasible small/medium/large workloads and document unavailable 100k+/network/removable/privileged scenarios without inventing results.
- [x] 3.4 Add the supported/tested/untested/unsupported Windows, architecture, DPI, theme, High Contrast, network, removable-volume, and clean-host matrix to release documentation. (`docs/quality/SUPPORT_MATRIX.md`)

## 4. Release-candidate pipeline

- [x] 4.1 Add a repository-consistent release-candidate entry point that composes cleanliness, version, configure/build, test, analysis, and packaging checks with bounded staging cleanup. (`verify/verify.ps1 release-candidate`, `verify/release/Build-ReleaseCandidate.ps1`)
- [x] 4.2 Extend release tooling with deterministic artifact names, signed/unsigned classification, SHA-256 checksum output, and a machine-readable provenance manifest containing commit/version/configuration/architecture. (`verify/release/Build-ReleaseCandidate.ps1`)
- [x] 4.3 Enforce `VERSION` consistency across staged executables, installer metadata, artifact names, manifest, and release-note metadata; add focused mismatch tests. (`Test-ReleaseVersionMetadata`, candidate manifest/archive/note generation)
- [x] 4.4 Ensure production mode fails closed for missing, placeholder, malformed, or mismatched certificate/pin inputs while unsigned local preparation remains explicitly non-production. (`Build-ReleaseCandidate.ps1`; existing `ReleaseCommon.psm1`/`sign-release.ps1`)
- [x] 4.5 Evaluate and, if valuable without heavyweight infrastructure, emit a structured dependency inventory/SBOM; otherwise document the measured decision. (`dependency-inventory.json`, explicitly not represented as a complete SPDX/CycloneDX SBOM)
- [x] 4.6 Add maintainable release-notes template and clean-host validation/cleanup handoff with known limitations and external prerequisites. (`docs/release/README.md`, `docs/release/RELEASE_NOTES_TEMPLATE.md`, existing `verify/release/CleanHostKit.README.md`)
- [x] 4.7 Add focused PowerShell tests for release-candidate classification, manifest/checksum reproducibility, version mismatch, and fail-closed signing prerequisites. (`verify/release/tests/Test-ReleaseCandidate.ps1`, PASS under `VsDevCmd.bat`; existing `Test-ReleaseTooling.ps1` covers strict version/pin mismatch seams.)

## 5. Integration and final evidence

- [ ] 5.1 Fix only newly observed P0/P1/P2 dogfood defects, preserving security boundaries, and add a regression test plus runtime evidence for each fix.
- [ ] 5.2 Run focused tests after each implementation wave, then Debug CTest, release-test CTest, `/analyze`, ASan parser/IPC targets, verifier doctor/build, OpenSpec strict validation, source/policy/doc-drift guards, and `git diff --check`.
- [ ] 5.3 Run one final uninterrupted end-user acceptance session on final binaries where the host permits, record the exact skipped external prerequisites, and update the campaign ledger/workstate.
- [ ] 5.4 Reconcile with `origin/main`, commit logical campaign waves, push without force, verify `HEAD == origin/main` and a clean tree, and inspect the hosted run for the pushed SHA.
