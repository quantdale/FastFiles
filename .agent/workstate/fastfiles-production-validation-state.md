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
- Logical status: `READY_FOR_AUTHORIZED_SIGNER_PROVISIONING` (PV-BUG-10 and
  PV-BUG-11 are locally resolved; production signer, pins, and timestamp URL
  remain external and unused).
- Started: 2026-08-14.
- Intended branch: `main`.
- Starting observed source/remote state for this session: `8c369dcd13693c2cc1c85d86c7ae23054a3ae3e1` on
  `main`, clean and equal after fetch. Re-derive this live on resume; it is
  historical evidence only.
- This session's implementation is intentionally uncommitted in this
  checkpoint; the final pushed SHA must be derived after commit and push rather
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
| PV-19 | PASS (local; final hosted run pending) | Production certificate policy, diagnostic-bypass source/configuration gate, timestamp policy, and expiry evidence passed; CurrentUser-root fixture reruns are blocked only by this host's stale CryptSvc/ProtectedRoots state. |
| PV-BUG-01 | PASS | Encrypted PFX loading fixed and covered by a temporary non-production encrypted-PFX dry-run. |
| PV-BUG-02 | PASS | Manifest-only detached CMS mode added; combined PE/manifest mode now fails closed. |
| PV-BUG-03 | PASS | Readiness pin provenance now uses its inspected artifact root. |
| PV-BUG-04 | PASS | Readiness overall status now has explicit FAIL/PASS/BLOCKED_EXTERNAL decisions. |
| PV-BUG-05 | PASS | Release signer preflight now requires a non-self-signed currently valid leaf, trusted Windows chain, Code Signing EKU, and private key for signing; detached CMS uses the same default policy, while tests use only a private custom-root seam. |
| PV-BUG-06 | PASS | Logical state and ExecPlan no longer retain the prior `PV-18=VALIDATING`/already-completed push frontier; prior and current slices are separated without self-referential SHA state. |
| PV-BUG-07 | PASS | `Test-UsableThumbprint` now explicitly rejects all-zero expected thumbprints; release-tooling coverage proves placeholder, malformed, and mismatched values fail before signing. |
| PV-BUG-08 | PASS | PowerShell collection/OID mutation return values were leaking into the production certificate-policy helper pipeline; all are now suppressed and detached CMS exact-signer validation passes through the private non-production custom-root seam. |
| PV-BUG-09 | PASS | The disposable expired-signer fixture's full test-PE signing could consume the short validity window on release-test; it now signs compact `ffprotocol_tests.exe`, retains 120 seconds, and cleans both Debug/release-test fixtures. |
| PV-BUG-10 | PASS | Signing-credential preflight remains current-valid/trusted/Code-Signing-EKU/private-key/exact-pin strict, while signed PE verification trusts Windows' timestamp-aware Authenticode result and does not reapply current-validity to an expired timestamped leaf. Real PE and detached CMS signing now fail closed without an explicit RFC3161 URL, and every final artifact is rechecked for exact signer and timestamp validity. |
| PV-BUG-11 | PASS | The runtime fixture trusts only its unique public root in `CurrentUser\Root`, proves exact-pin acceptance before `NotAfter`, rejects the unchanged no-timestamp PE after expiry through production WinTrust, and separately proves a SignTool RFC3161-timestamped PE remains accepted after leaf expiry. The timestamped case uses a worker/parent cleanup boundary; repeat execution on this already-used host is environmentally blocked at root insertion by stale CryptSvc/ProtectedRoots state. |

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
- `pwsh ./verify/verify.ps1 build -SkipAnalyze` (historical checkpoint): PASS.
  The current source was separately rebuilt through the Debug, release-test,
  and analyze presets; the current full harness reaches the known
  CurrentUser-root fixture limitation before completion.
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
- `pwsh ./verify/verify.ps1 build` (2026-08-14): environment-blocked on the
  current host after reaching the trust-store-mutating Authenticode fixture;
  the exact spawned processes were stopped and their state paths cleaned. This
  is the same stale CryptSvc/ProtectedRoots condition recorded in Blockers,
  not a product assertion failure.
- `pwsh ./verify/verify.ps1 doctor` (2026-08-14): PASS for the discovered
  Visual Studio VC toolset, Windows SDK, CMake, Ninja, and CTest; optional
  diagnostic tools were reported as skipped when absent.
- `ctest --test-dir build/debug -R ffengine_authenticode_verification_tests
  --output-on-failure` in the discovered VS environment (2026-08-14): PASS
  1/1 on the clean-enough pre-worker run; the real WinTrust fixture covered
  trusted pre-expiry acceptance, mismatched/placeholder pins, unchanged-byte
  no-timestamp expiry rejection, and an untrusted signer. All fixture cleanup
  checks passed.
- Timestamped WinTrust worker observation (2026-08-14): a real SignTool
  RFC3161-timestamped PE was accepted by
  `ffsetup::VerifyAuthenticodeAndGetThumbprintAtPath` and
  `ffsetup::VerifyPinnedSignature` before and after the leaf expired without
  changing the PE bytes. The final C++ expectation and parent-process cleanup
  boundary are implemented. Repeating the test on this host is blocked before
  the assertion at `CurrentUser\Root` insertion by stale CryptSvc/ProtectedRoots
  state; no fixture material remains.
- Package-trust fixture focused run (2026-08-14): the native C++ verifier and
  PowerShell verifier passed the timestamped detached CMS before the host's
  CurrentUser root insertion became blocked; the final fixture now exercises
  the actual SignTool `/p7` path and parent cleanup state protocol.
- Current Debug and release-test CTest safe subsets (2026-08-14): PASS 51/51
  each after excluding the three trust-store-mutating fixture tests; both
  documentation-drift tests pass after normalizing the edited C++ files.
  The full fixture tests remain blocked at CurrentUser\Root insertion by the
  host condition recorded above.
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
- `PV-BUG-11`: expired-signer runtime evidence was confounded by an untrusted
  root. Fixed with a unique public root in `CurrentUser\Root`, a real trusted
  pre-expiry WinTrust acceptance followed by unchanged no-timestamp rejection,
  a separate timestamp-longevity worker, and state-driven cleanup.

## Blockers

- No explicitly authorized production signer configuration is available. Only
  uniquely named temporary non-production test certificates were created and
  all were removed; no production certificate or PFX was selected.
- The machine is an `ELEVATED_DEV_HOST`, not a clean host, because pre-existing
  FastFiles resources are present. They were not removed for this campaign.
- The Computer Use native pipe is unavailable, preventing direct screenshots.
- No second genuinely signed release exists for upgrade/rollback validation.
- The already-used development host's `CurrentUser\Root` provider path is
  blocked by stale CryptSvc/ProtectedRoots state during repeat fixture root
  insertion. This prevents a final local rerun of the trust-store-mutating
  CTest cases; it is not a repository or production-signer blocker, and exact
  temporary certificates/files were removed.

## Next exact action

Obtain/provide the explicitly authorized production signer configuration and
execute `verify/release/sign-release.ps1 -DryRun` against the exact final
candidate; keep real pins, production signing, and signed runtime evidence
behind that external authorization boundary.
