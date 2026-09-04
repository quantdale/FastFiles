# Proposal: Certification Truthfulness Repair and Recertification

## Why

Independent repository review identified 10 material certification-integrity and fail-open defects in FastFiles' external certification and durable stress evidence systems:
1. **CT-001**: `Invoke-LongRunCertification.ps1` produces a false PASS when CTest matches zero tests, because CTest exits with code 0 on empty matches.
2. **CT-002**: `Invoke-ResourceSoak.ps1` emits synthetic activity transitions with `details = "Simulated periodic UI activity: ..."` and `success = true` without driving FastFiles.
3. **CT-003**: `Invoke-ResourceSoak.ps1` declares PASS purely on process survival and non-zero samples, completely ignoring severe resource runaway or leak slopes.
4. **CT-004**: Resource soak baseline is captured at startup (sample 0 at t=0.5s), conflating normal CRT/D2D/COM initialization with steady-state leaks.
5. **CT-005**: Prior durable stress evidence was captured against an uncommitted dirty tree and recorded `exactTreeBound: false`, yet was summarized as durable closure evidence.
6. **CT-006**: OpenSpec task 3.2 required launch/ready/shutdown x100 and rapid-input x50, but neither was executed nor retained in machine-readable form.
7. **CT-007**: External frontier tasks are semantically mismatched (interactive tasks mapped to nonexistent `ffuia_smoke_tests`; Hyper-V/VM provider tasks mapped to a vision-critic script).
8. **CT-008**: `external-certification-readiness-and-evidence-convergence` in `openspec/lifecycle.json` lacks authoritative git-commit and hosted-run bindings.
9. **CT-009**: Prose summaries and markdown reports diverged from machine-readable JSON artifacts.
10. **CT-010**: The two-pass self-hashing design in `Invoke-LongRunCertification.ps1` and `Invoke-ResourceSoak.ps1` modified the file after hashing, corrupting the recorded hash.

## What Changes

* **Long-Run Discovery & Zero-Test Fail-Closed**: Harden `Invoke-LongRunCertification.ps1` with CTest enumeration discovery (`--show-only=json-v1`), requiring `tests.Count -eq 1`, verifying execution proof per iteration, and supporting direct test executables.
* **UIA Long-Run Adapter & Real Inputs**: Implement `Invoke-UiaLongRunCertification.ps1` to execute real FastFiles UI scenarios with semantic readiness, foreground assertions, and bounded iterations. Implement application lifecycle x100 and rapid-input x50 runners.
* **Provider Taxonomy & Frontier Audit**: Separate `Invoke-EnvironmentProviderCertification.ps1` (Hyper-V, local) from `Invoke-VisionProviderCertification.ps1` (visual critic). Re-audit all 25 external frontier tasks and validate command executability.
* **Soak Engine Redesign**: Rebuild `Invoke-ResourceSoak.ps1` around a 7-phase lifecycle (`LAUNCH`, `READY_WAIT`, `WARMUP`, `STEADY_STATE_BASELINE`, `MEASURED_SOAK`, `SHUTDOWN`, `POST_SHUTDOWN_RESIDUE_CHECK`). Replace simulated activity with real UIA transitions or explicit `-IdleOnly` mode. Implement steady-state slope and stability gates, engine PID tracking, and graceful shutdown verification.
* **Artifact Hash Integrity & Sanitization**: Fix artifact hashing via external manifest (`evidence-manifest.json`) or canonical exclusion, sanitize build paths to logical repo-relative paths, and generate markdown summaries strictly from JSON.
* **Clean Implementation & Authoritative Recertification**: Commit all tooling repairs on a clean tree, re-run full stress/soak suite with `-RequireExactTree`, retain machine-readable artifacts, reconcile OpenSpec/lifecycle.json, and achieve hosted exact-SHA certification.

## Impact

* **Tooling**: Updates to `verify/certification/*` and new tests under `verify/certification/tests/`.
* **Evidence**: Authoritative exact-tree evidence under `openspec/changes/certification-truthfulness-repair-and-recertification/evidence/`.
* **Product Code Freeze**: Zero modifications to product C++ code unless certification exposes a real defect.

## Non-Goals

* Fabricating passes for external hardware/cloud providers without real environments.
* Weakening resource stability thresholds or ignoring leaks.
* Modifying product C++ code opportunistically without defect proof.
