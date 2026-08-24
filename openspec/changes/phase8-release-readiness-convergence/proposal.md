## Why

FastFiles has substantial verification and release tooling, but its project status is still partly maintained as prose and its provider contract stops short of deterministic readiness diagnostics. That makes it easy for future agents or CI to mistake stale test counts, open external work, or an unavailable Windows guest for a current release verdict. Phase 8 needs one machine-derived frontier that is honest about PASS, FAIL, SKIPPED, BLOCKED_ENVIRONMENT, and REQUIRED-BUT-UNAVAILABLE while keeping the degraded product path and production signing boundary intact.

## What Changes

- Add a structured project-status generator and Markdown projection that derive Git state, CTest inventory/results, OpenSpec task state/classification, capability availability, release-candidate state, and explicit blockers.
- Add status-drift tests so manually stale test counts and project-frontier claims fail the repository hygiene gate.
- Harden environment-provider descriptors and lifecycle orchestration with deterministic preflight reason codes, safe user-supplied image metadata, bootstrap-plan generation without secrets, and a fake provider contract suite.
- Expand hosted CI with bounded OpenSpec, policy, status, provider-contract, release-candidate, and portable trust/reproducibility gates without production signer material.
- Produce one sanitized release-readiness/frontier report that separates locally certified work from environment, provider, signing, and clean-host/elevated blockers.

## Capabilities

### New Capabilities

- `project-status-and-frontier`: machine-generated repository status, task classification, drift detection, and release-readiness projection.
- `provider-preflight-contract`: deterministic provider preflight, lifecycle contract tests, user-supplied image metadata, and disposable bootstrap planning.
- `release-qualification-pipeline`: bounded CI/release qualification composition for non-production candidates and safe structured evidence.

### Modified Capabilities

- None. Existing product and privileged-path contracts remain unchanged; this change adds verification/reporting contracts around them.

## Impact

Affected areas are `verify/core`, `verify/providers`, `verify/release`, `verify/verify.ps1`, CI workflow configuration, repository hygiene tests, release documentation, and new OpenSpec/evidence files. No production IPC command, Authenticode policy, trust store, service privilege, or UI↔engine behavior changes.
