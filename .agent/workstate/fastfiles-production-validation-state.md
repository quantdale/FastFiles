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
- Logical status: `LOCAL_SIGNER_VALIDATION_BLOCKED` (PV-BUG-10 is locally
  validated; PV-BUG-11 remains blocked on a real trusted WinTrust fixture on
  this host).
- Started: 2026-08-14.
- Intended branch: `main`.
- Starting observed source/remote state for this session: `8c369dcd13693c2cc1c85d86c7ae23054a3ae3e1` on
  `main`, clean and equal after fetch. Re-derive this live on resume; it is
  historical evidence only.
- The signer-hardening implementation and the follow-up CurrentUser trust-store
  fixture repair remain uncommitted; the trust-store repair is not yet
  validated. The final pushed SHA must be derived after commit and push rather
  than embedded in the state file.
- Historical hosted baseline supplied at session start: run `31776442664` for
  the independently reviewed baseline; the final signer-hardening run must be
  queried by its live `head_sha` at handoff.
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
| PV-18 | PASS (prior slice) | The independently reviewed/pushed baseline was `8c369dcd13693c2cc1c85d86c7ae23054a3ae3e1`; its live SHA/CI remain derived at resume. |
| PV-19 | NOT VERIFIED | Production certificate policy, diagnostic-bypass source/configuration gate, and timestamp policy have implementation/evidence from the prior slice, but the current trust-store fixture does not yet produce a trusted pre-expiry real-WinTrust acceptance on this host; final focused and hosted validation is pending. |
| PV-BUG-01 | PASS | Encrypted PFX loading fixed and covered by a temporary non-production encrypted-PFX dry-run. |
| PV-BUG-02 | PASS | Manifest-only detached CMS mode added; combined PE/manifest mode now fails closed. |
| PV-BUG-03 | PASS | Readiness pin provenance now uses its inspected artifact root. |
| PV-BUG-04 | PASS | Readiness overall status now has explicit FAIL/PASS/BLOCKED_EXTERNAL decisions. |
| PV-BUG-05 | PASS | Release signer preflight now requires a non-self-signed currently valid leaf, trusted Windows chain, Code Signing EKU, and private key for signing; detached CMS uses the same default policy, while tests use only a private custom-root seam. |
| PV-BUG-06 | PASS | Logical state and ExecPlan no longer retain the prior `PV-18=VALIDATING`/already-completed push frontier; prior and current slices are separated without self-referential SHA state. |
| PV-BUG-07 | PASS | `Test-UsableThumbprint` now explicitly rejects all-zero expected thumbprints; release-tooling coverage proves placeholder, malformed, and mismatched values fail before signing. |
| PV-BUG-08 | PASS | PowerShell collection/OID mutation return values were leaking into the production certificate-policy helper pipeline; all are now suppressed and detached CMS exact-signer validation passes through the private non-production custom-root seam. |
| PV-BUG-09 | PASS | The disposable expired-signer fixture's full test-PE signing could consume the short validity window on release-test; it now signs compact `ffprotocol_tests.exe`, retains 120 seconds, and performs exact cleanup. The subsequent runtime transition remains separately blocked by the host's protected-root policy. |
| PV-BUG-10 | PASS | Signing-credential preflight remains current-valid/trusted/Code-Signing-EKU/private-key/exact-pin strict, while signed PE verification trusts Windows' timestamp-aware Authenticode result and does not reapply current-validity to an expired timestamped leaf. Real PE and detached CMS signing require an explicit RFC3161 URL and post-sign verification; the current release-tooling regression passed with zero failures. |
| PV-BUG-11 | BLOCKED_ENVIRONMENT / NOT VERIFIED | The fixture currently cannot establish a trusted pre-expiry real-WinTrust acceptance on this host: production WinTrust returns `0x800B0109` (`CERT_E_UNTRUSTEDROOT`) even after the exact generated root is written through the attempted CurrentUser path. The host's `CurrentUser\\Root\\ProtectedRoots` registry state is user-read-only; no ACL, policy, or system trust store was changed. The unchanged-byte expiry transition and exact cleanup checks are useful fixture evidence but do not prove the required production-path transition until an isolated supported trust location is found. |

## Known OpenSpec boundary

`openspec/changes/close-independent-validation-gaps/tasks.md` still leaves 6
tasks open: 4.2 remains open only for its external release-pipeline signer/pin
provisioning clause; 4.5 (signed/elevated authenticated install/runtime), 4.6
(real MFT/USN), 4.7 (`fftest` against the running service), 4.8 (clean-host
matrix), and 5.1 (direct rendered matrix plus real aggregate-backed percentage
sum). The local 4.2 clauses are evidenced in
`evidence/authentication-negative-tests.md`; no checkbox is marked complete
without the remaining external acceptance evidence. `recovery-degraded-browsing/tasks.md`
is fully checked.

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
- Current-source configure/build gates (2026-08-15): Debug, release-test, and
  analyze configure/build completed successfully. The ASAN protocol-fuzz target
  built and its CTest run passed 1/1 inside the discovered Visual Studio
  developer environment.
- `pwsh ./verify/release/tests/Test-ReleaseTooling.ps1 -BuildRoot
  ./build/release-test` (2026-08-14): PASS with zero failures after the four
  repairs and the certificate-policy/expected-thumbprint regression matrix;
  default Windows trust covers self-signed/untrusted cases, the trusted-root
  EKU/validity cases use only an in-memory custom root, and the real SignTool
  `timestamp /p7` loopback responder proves the final detached CMS format.
- Release-tooling timestamp matrix (2026-08-14): PASS. A real non-DryRun
  signing probe fails before mutation with `PRODUCTION_TIMESTAMP_REQUIRED`;
  the signed-artifact policy keeps an expired leaf valid when Authenticode is
  reported WinTrust-valid with a timestamp; valid/invalid/missing/wrong-signer
  timestamp states and an expired manifest leaf with a pre-expiry timestamp
  are covered.
- `pwsh ./verify/check_diagnostic_policy.ps1` (2026-08-14): PASS; Debug-only
  diagnostic scope, release/release-test preset state, hosted-CI invocation,
  and existing Release caches are all fail-closed.
- `pwsh ./verify/release/tests/Test-ReleaseTooling.ps1 -BuildRoot
  ./build/release-test` (2026-08-15): PASS with zero failures on the current
  tree. This validates the separate signing-credential and signed-artifact
  policies, required production timestamp gate, real SignTool `/p7` detached
  CMS timestamp format, exact signer/timestamp postchecks, and the timestamp
  longevity matrix.
- `pwsh ./verify/check_diagnostic_policy.ps1 -RepoRoot .` (2026-08-15): PASS
  with zero failures.
- Focused Debug build of `ffengine_authenticode_verification_tests` and
  `ffsetup_package_trust_tests` (2026-08-15): PASS. The repaired fixture
  scripts deploy and the C++ targets compile/link cleanly.
- `ctest --test-dir build/debug -R
  "ffengine_authenticode_verification_tests|ffengine_authenticode_verification_timestamp_tests|ffsetup_package_trust_tests"
  --output-on-failure` (2026-08-15): FAIL, 0/3. The engine prepare,
  timestamp-worker, and native package-CMS prepare steps fail at the bounded
  CurrentUser trust setup before positive acceptance; the engine's untrusted
  signer/pin negatives and all exact cleanup checks pass.
- `ctest --preset debug --output-on-failure` (2026-08-15): 51/54 passed. The
  only failures were `ffengine_authenticode_verification_tests`,
  `ffengine_authenticode_verification_timestamp_tests`, and
  `ffsetup_package_trust_tests`; each failed before trusted acceptance because
  the CurrentUser Root setup was blocked. All other Debug tests passed.
- `ctest --test-dir build/release-test --output-on-failure` (2026-08-15):
  51/54 passed with the same three protected-root fixture failures; all other
  release-test tests passed.
- `pwsh ./verify/verify.ps1 build` (2026-08-15): FAIL because its aggregate
  build capability includes those same three repository test failures. This is
  not recorded as a product build failure; the direct configure/build gates
  passed above.
- `pwsh ./verify/verify.ps1 doctor` (2026-08-14): PASS for the discovered
  Visual Studio VC toolset, Windows SDK, CMake, Ninja, and CTest; optional
  diagnostic tools were reported as skipped when absent.
- Previous trust-store focused runs were exploratory and must not be treated as
  final evidence. The current native CurrentUser helper still leaves real
  WinTrust at `CERT_E_UNTRUSTEDROOT` before expiry on this host, so the expected
  positive runtime assertions fail. The custom-root chain validity and
  unchanged-byte expiry-transition checks do not substitute for production
  WinTrust acceptance. Exact cleanup was performed after the failed run; a
  final residue audit is required before handoff.
- Documentation drift, source-marker policy, PowerShell parsing, strict
  OpenSpec validation (45/45), and diagnostic policy all passed on the current
  tree. The focused release-tooling suite passed with zero failures.
- The prior repair baseline and hosted run supplied at session start were
  `8c369dcd13693c2cc1c85d86c7ae23054a3ae3e1` and `31776442664`; query the
  final signer-hardening run by its live `head_sha` at handoff.
- Canonical privileged driver (2026-08-14): elevation PASS, but package
  signature missing, authorized signing missing, pins missing, and clean-host
  preflight failed; it stopped before installation. The read-only validator
  separately returned `CLEAN_HOST_PRECONDITION_FAILED` with
  `installExecuted=false` and `cleanupExecuted=false`.
- ASAN protocol fuzz (2026-08-14): the first CTest call outside the VS
  developer environment lacked the ASAN runtime (`0xc0000135`); the same test
  rerun inside `VsDevCmd` passed 1/1. This was an invocation-environment issue,
  not a product failure.
- Analyze configure/build, `verify.ps1 doctor`, strict OpenSpec validation
  (45/45), diagnostic policy, PowerShell parsing, source markers, and
  documentation/source policy checks passed for the current source.
- Direct rendered UI attempt (2026-08-14): Computer Use native pipe was
  unavailable on the initial call and one retry; no screenshot or interactive
  evidence was produced.

- Current trust-fixture blocker (2026-08-15): a bounded focused engine run
  reported production WinTrust status `0x800B0109` (`CERT_E_UNTRUSTEDROOT`)
  for the generated root before the deliberate leaf-expiry transition. Direct
  registry/provider experiments, default .NET CurrentUser Root, certutil
  addstore, and a registered temporary physical store did not yet yield a
  supported real-WinTrust trust result. The host's ProtectedRoots ACL denies
  the interactive user write access. No protected-root blob, ACL, LocalMachine
  store, or policy store was modified.
- A follow-up Crypt32 probe used `CERT_SYSTEM_STORE_UNPROTECTED_FLAG` while
  opening the isolated CurrentUser Root provider; that provider also hung on
  this host. The probe was terminated, and its two generated personal-store
  certificates/private keys were removed and verified absent. This does not
  substitute for the required production WinTrust transition.

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
- `PV-BUG-05`: production certificate preflight previously treated
  Subject/Issuer shape as sufficient. Fixed with explicit current-validity,
  non-self-signed, Code Signing EKU, trusted-chain, and signing-private-key
  checks; detached CMS validation reuses the policy.
- `PV-BUG-06`: stale workstate/ExecPlan frontier retained an already-completed
  push action. Fixed by separating the prior PASS slice from this local
  signer-hardening slice and deriving live Git/CI externally.
- `PV-BUG-07`: the all-zero expected-thumbprint path was not unambiguously
  rejected by `Test-UsableThumbprint`. Fixed with explicit boolean evaluation.
- `PV-BUG-08`: the certificate-policy helper initially emitted collection/OID
  mutation results into the PowerShell pipeline, corrupting the detached CMS
  policy object. Fixed by suppressing those outputs and adding a CMS regression.
- `PV-BUG-09`: the first short-lived expired-signer fixture could expire before
  `signtool` completed when signing the optimized full test PE. Fixed by using
  the compact protocol test PE plus a 120-second currently-valid window before
  the deliberate expiry transition; no production certificate policy was
  weakened.
- `PV-BUG-10`: signing-credential current-validity policy was being reused for
  already-signed timestamped artifacts, and real signing could omit a
  timestamp. Fixed with separate policy functions, a required operator-supplied
  RFC3161 URL for real PE/CMS signing, actual SignTool `/p7` manifest
  timestamping, and post-sign exact-signer/timestamp verification.
- `PV-BUG-11`: expired-signer runtime evidence remains open locally. The
  fixture has a unique public root, unchanged-byte expiry transition, separate
  timestamp-longevity worker, and state-driven cleanup, but real WinTrust still
  reports `CERT_E_UNTRUSTEDROOT` before expiry on this protected-root host. Do
  not close this finding until a supported isolated trust location proves the
  valid-trusted acceptance followed by same-PE expiry rejection and exact
  root/leaf/private-key/file cleanup.

## Blockers

- No explicitly authorized production signer configuration is available. Only
  uniquely named temporary non-production test certificates were created and
  all were removed; no production certificate or PFX was selected.
- The machine is an `ELEVATED_DEV_HOST`, not a clean host, because pre-existing
  FastFiles resources are present. They were not removed for this campaign.
- The Computer Use native pipe is unavailable, preventing direct screenshots.
- No second genuinely signed release exists for upgrade/rollback validation.
- The current host's protected-root policy prevents the generated CurrentUser
  root from becoming trusted by production WinTrust. The bounded certutil
  operation and the documented Crypt32 unprotected-store probe both block;
  no ProtectedRoots ACL/blob, LocalMachine store, or policy store was modified.
  The focused/full tests fail closed before positive acceptance and pass their
  exact cleanup checks.

## Next exact action

Commit and push the validated changes, then inspect hosted CI by the final head
SHA. If the hosted environment reproduces the same protected-root condition,
keep PV-BUG-11 as `BLOCKED_ENVIRONMENT / NOT VERIFIED` and the
signer-provisioning gate closed; otherwise reconcile the hosted real-WinTrust
evidence before changing this state.
