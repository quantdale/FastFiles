# FastFiles Production-Validation Campaign State

## State protocol

This file stores logical campaign state and sanitized historical evidence. Live
branch, `HEAD`, `origin/main`, dirty status, ancestry, and hosted CI are derived
on resume and handoff. This file must not contain the SHA of its own containing
commit as current state. Never record credentials, private keys, PFX files,
passwords, tokens, raw indexed paths, or sensitive logs.

## Resume commands

Use `git fetch --prune origin`, `git status --short`, `git branch --show-current`,
`git rev-parse HEAD`, `git rev-parse origin/main`, and
`git log --oneline --decorate -10` before trusting live-state statements below.

## Campaign identity

- Campaign: FastFiles production validation.
- Logical status: `VALIDATING`.
- Started: 2026-08-14.
- Intended branch: `main`.
- Starting observed source/remote state: `fb26f34` on `main`, clean and equal
  after fetch. Re-derive this live on resume; it is historical evidence only.
- Validated implementation anchor: `f9ea9948a65f06aa1366ce488ca38db1c76fdbf3`.
- Validated release-tooling repair baseline: `b692f5f0bf2ac2fc97857c1d92918e573eca1501`.
  This is a historical code-bearing commit, not a current-HEAD claim for this
  state file.
- Historical hosted implementation evidence: run `31724666654` was green for
  its referenced SHA; the current branch run must be queried again.
- Prior handoff: FF-POST-01 through FF-POST-12 were validated; production gates
  were not claimed.

## Environment classification

| Property | Status | Evidence / reason |
|---|---|---|
| Administrator token | `PASS` | Explicit token probe: Administrator role true, Administrators group present, High Mandatory Level, AMD64. |
| Clean host | `BLOCKED_ENVIRONMENT` | Canonical read-only preflight reports prior FastFiles state; no cleanup was performed to manufacture clean-host evidence. |
| Contamination | `PRESENT` | service, processes, scheduledTask, clientGroup, installDirectory, programData, userData, uninstallRegistry. |
| Desktop/UI screenshot capability | `BLOCKED_ENVIRONMENT` | Computer Use native pipe was unavailable on the initial call and one retry; no screenshot was captured. |
| Production signer | `BLOCKED_EXTERNAL` | No explicit authorized CertificateStore thumbprint or external PFX configuration was supplied; no certificate was selected. |

## Validation matrix

| ID | Status | Sanitized evidence / blocker |
|---|---|---|
| PV-01 | PASS | Administrator role, Administrators group, High Mandatory Level, and AMD64 were explicitly observed. |
| PV-02 | PASS (historical unsigned candidate) | The pre-handoff release-test candidate had four x64 executables, product `0.1.0`, file `0.1.0.0`, and source identity `fb26f34`; all were explicitly unsigned. Final live candidate provenance is derived after the last pushed SHA. Production Release configure remains fail-closed without pins. |
| PV-03 | BLOCKED_EXTERNAL | No explicit authorized CertificateStore thumbprint or external PFX configuration. |
| PV-04 | BLOCKED_EXTERNAL | No real production signing was attempted; the dry-run correctly stopped with `AUTHORIZED_SIGNER_REQUIRED`. |
| PV-05 | BLOCKED_EXTERNAL | Exact final signed Engine/Service artifacts and approved leaf provenance are unavailable. |
| PV-06 | BLOCKED_EXTERNAL | Authorized detached CMS signing and final package validation are unavailable. |
| PV-07 | BLOCKED_ENVIRONMENT | Canonical preflight found the existing FastFiles service/process/task/group/install/ProgramData/user-state/uninstall resources. Classification: `ELEVATED_DEV_HOST`. |
| PV-08 | BLOCKED_EXTERNAL | Elevated privileged driver stopped before installation because the package signature was missing; no machine mutation occurred. |
| PV-09 | BLOCKED_EXTERNAL | No signed installer transaction was available; pre-existing service state was not changed. |
| PV-10 | BLOCKED_EXTERNAL | No signed mutually authenticated Engine↔service connection or heartbeat evidence was available. |
| PV-11 | BLOCKED_EXTERNAL | MFT requires separate evidence on the authenticated runtime. |
| PV-12 | BLOCKED_EXTERNAL | USN requires separate journal, mutation, cursor, and resume evidence. |
| PV-13 | BLOCKED_EXTERNAL | Requires the signed runtime and exact-process restart/reconnect evidence. |
| PV-14 | BLOCKED_EXTERNAL | No second genuinely signed release identity/fixture is available. |
| PV-15 | BLOCKED_EXTERNAL | Canonical uninstall was not run against this contaminated development host. |
| PV-16 | BLOCKED_EXTERNAL | No importer-accepted PASS bundle exists. |
| PV-17 | BLOCKED_ENVIRONMENT | Direct rendered validation was attempted through Computer Use; its native pipe was unavailable twice, so no screenshots were captured. |
| PV-18 | VALIDATING | Repository tests, OpenSpec, doctor, release-tooling, candidate provenance, and hygiene are passing; commit/push and hosted-run review remain. |
| PV-BUG-01 | PASS | Encrypted PFX loading fixed and covered by a temporary non-production encrypted-PFX dry-run. |
| PV-BUG-02 | PASS | Manifest-only detached CMS mode added; combined PE/manifest mode now fails closed. |
| PV-BUG-03 | PASS | Readiness pin provenance now uses its inspected artifact root. |
| PV-BUG-04 | PASS | Readiness overall status now has explicit FAIL/PASS/BLOCKED_EXTERNAL decisions. |

## Known OpenSpec boundary

`openspec/changes/close-independent-validation-gaps/tasks.md` still leaves 6
tasks open: 4.2 (expired/untrusted signer fixtures and release-pipeline
evidence), 4.5 (signed/elevated authenticated install/runtime), 4.6 (real
MFT/USN), 4.7 (`fftest` against the running service), 4.8 (clean-host matrix),
and 5.1 (direct rendered matrix plus real aggregate-backed percentage sum).
`recovery-degraded-browsing/tasks.md` is fully checked. No checkbox changed in
this campaign because none of the remaining acceptance evidence exists.

## Evidence ledger

- Token probe (2026-08-14): PASS. Identity and group names were sanitized; no
  credentials or tokens were recorded.
- `ReleaseReadiness.ps1 -Mode report -ArtifactRoot ./build/release-test
  -OutputFormat json` (2026-08-14): artifact integrity PASS for the
  pre-handoff candidate, four unsigned signature rows, pin state MISSING from
  that same root, clean-host/signing BLOCKED_EXTERNAL, exit 2. The older
  `build/release` output was stale and was not used as a production candidate.
- Isolated current-source production Release configure (2026-08-14): failed
  closed with the documented missing/all-zero pin error. No placeholder values
  were supplied and no production Release build was claimed.
- `pwsh ./verify/verify.ps1 build -SkipAnalyze` (2026-08-14): PASS. Debug and
  release-test builds completed; release-test identity, x64 architecture,
  version metadata, and four-file inventory were inspected separately.
- `pwsh ./verify/release/tests/Test-ReleaseTooling.ps1 -BuildRoot
  ./build/release-test` (2026-08-14): PASS with zero failures after the four
  repairs.
- The repair baseline was pushed to `origin/main`; its hosted CI run was
  observed as run `31758898989` for the exact repair SHA while the first
  inspection was still in progress. Query the live run again at handoff.
- Canonical privileged driver (2026-08-14): elevation PASS, but package
  signature missing, authorized signing missing, pins missing, and clean-host
  preflight failed; it stopped before installation. The read-only validator
  separately returned `CLEAN_HOST_PRECONDITION_FAILED` with
  `installExecuted=false` and `cleanupExecuted=false`.
- ASAN protocol fuzz (2026-08-14): the first CTest call outside the VS
  developer environment lacked the ASAN runtime (`0xc0000135`); the same test
  rerun inside `VsDevCmd` passed 1/1. This was an invocation-environment issue,
  not a product failure.
- Debug and release-test CTest (2026-08-14): each passed 53/53. Analyze
  configure/build, `verify.ps1 doctor`, strict OpenSpec validation (45/45), and
  documentation/source policy checks passed.
- Direct rendered UI attempt (2026-08-14): Computer Use native pipe was
  unavailable on the initial call and one retry; no screenshot or interactive
  evidence was produced.

## Repository-owned failures

- `PV-BUG-01`: encrypted PFX password was not supplied to `Get-PfxCertificate`.
  Fixed in `verify/release/sign-release.ps1`; regression uses temporary
  non-production material and cleans it after the test.
- `PV-BUG-02`: combined PE and detached-manifest signing could leave stale
  manifest hashes after a later PE mutation. Fixed with `-ManifestOnly` and
  fail-closed combined-mode rejection; regressions cover no mutation and
  rejection.
- `PV-BUG-03`: readiness pin provenance ignored the requested artifact root.
  Fixed in `ReleaseReadiness.ps1`; a release-test-root report probe covers it.
- `PV-BUG-04`: readiness overall status was hardcoded to `BLOCKED_EXTERNAL`
  whenever artifacts were present. Fixed with exported
  `Get-ReleaseReadinessStatus` and pure outcome tests.

## Blockers

- No explicitly authorized production signer configuration is available. No
  certificate store was searched and no PFX was selected.
- The machine is an `ELEVATED_DEV_HOST`, not a clean host, because pre-existing
  FastFiles resources are present. They were not removed for this campaign.
- The Computer Use native pipe is unavailable, preventing direct screenshots.
- No second genuinely signed release exists for upgrade/rollback validation.

## Next exact action

Run final tracked-file diff/hygiene checks, create the focused commit for the
validated release-tooling repairs, and push it to `origin/main`; then checkpoint
the campaign docs in a later non-self-referential commit.
