# FastFiles Production-Validation Campaign ExecPlan

## Objective

Execute the production-validation campaign against the exact `main` source
baseline. Close only the release gates for which this environment produces
direct, sanitized evidence. Repair repository-owned release/runtime defects
when the real path exposes them; classify signer, clean-host, and desktop
requirements that cannot be satisfied here as external or environment
blockers. Finish with a clean commit pushed to `origin/main` and live hosted-CI
inspection.

This plan is a campaign record, not a signing or CI database. Live Git state,
remote state, and hosted Actions state are derived at resume and handoff time.
No private keys, PFX files, passwords, tokens, raw index dumps, or sensitive
machine logs belong in the repository.

## Authoritative context

- Repository guidance: `AGENTS.md`.
- Durable historical engineering record: `docs/agent/WORKSTATE.md`.
- Current OpenSpec source of truth: `openspec/changes/close-independent-validation-gaps/tasks.md`.
- Release architecture and security invariants: the foundation design and the
  `close-independent-validation-gaps` design/specs.
- Release tooling: `verify/release/ReleaseCommon.psm1`,
  `ReleaseReadiness.ps1`, `sign-release.ps1`, `derive-pin.ps1`,
  `New-ReleaseKit.ps1`, `validate-clean-host.ps1`,
  `privileged-release.ps1`, `cleanup-clean-host.ps1`, and
  `import-clean-host-result.ps1`.

## Non-negotiable invariants

- Never call a development/self-signed certificate a production signer.
- Never weaken placeholder-pin, Authenticode, CMS, package-root, reparse,
  service-ACL, closed-protocol, or degraded-mode fail-closed behavior.
- Release signer preflight must independently establish current certificate
  validity, non-self-signed leaf identity, trusted Windows chain, and Code
  Signing EKU; signing providers must also prove the private key and exact
  expected-thumbprint match. Revocation is reported separately as
  `NOT_CHECKED_OFFLINE` because this architecture does not introduce a hidden
  network dependency.
- Signing-credential eligibility is separate from signed-artifact verification.
  A new signer must satisfy current validity, trust, EKU, private-key, and
  exact-pin rules. An already-signed Authenticode artifact is judged by
  Windows' timestamp-aware WinTrust result; a valid RFC3161 timestamp must not
  be invalidated merely because its leaf is expired now.
- Real non-DryRun PE and detached-CMS signing requires an operator-supplied
  RFC3161 timestamp URL. Each final PE and the final manifest signature must be
  re-verified for signer identity, Authenticode/CMS validity, and timestamp
  presence/authenticity; a SignTool exit code alone is insufficient.
- A development host with prior FastFiles state is `ELEVATED_DEV_HOST`, never
  `CLEAN_HOST`, even after resources are removed.
- MFT, USN, authentication, uninstall, and rendered-UI evidence are separate
  claims. One cannot satisfy another.
- OpenSpec checkboxes change only when their exact acceptance evidence exists.
- Session-owned tracked changes are validated, committed, pushed without force,
  and left clean. The state file never records the SHA of the commit that
  contains itself as current state.

## Campaign sequence

1. Reconcile branch, remote, worktree, historical state, OpenSpec tasks, and
   release-tool contracts.
2. Prove the Windows administrator token explicitly and run the canonical
   read-only clean-host/release-readiness reports. Record elevation and host
   contamination independently.
3. Establish a current x64 Release candidate: source/version/build identity,
   PE architecture, version resources, expected four executables, hashes, and
   unsigned/production markers. Rebuild through the documented release flow if
   stale or mismatched.
4. Audit the signing DAG from final PE bytes through Authenticode signatures,
   leaf-pin provisioning, V2 manifest generation, detached CMS signature, kit
   generation, and final package validation. The prior audit repaired
   encrypted-PFX loading, stale manifest sequencing, wrong-root pin provenance,
   and a permanently blocked readiness status. The current signer-hardening
  slice adds Windows chain/current-validity/Code Signing EKU/private-key
  preflight, exact expected-thumbprint matching, detached-CMS policy reuse,
  and deterministic non-production certificate fixtures in
  `Test-ReleaseTooling.ps1`. PV-BUG-10 adds a timestamp-aware
  signed-artifact policy, a production timestamp gate, and detached CMS
  RFC3161 verification. PV-BUG-11 repairs the runtime fixture design so that,
  where the host permits it, its trusted pre-expiry/expired transition is a
  real WinTrust experiment rather than an untrusted-root negative test. The
  fixture installs only the exact generated public root into CurrentUser Root
  through the supported `certutil -user -f -addstore Root` path, checks/removes
  the exact thumbprint-derived registry key and temporary DER path, and keeps
  the real WinTrust trust result and exact cleanup checks separate. The current
  host still blocks the supported trust-store operation before that acceptance
  can be established.
5. Use only an explicitly authorized CertificateStore thumbprint or external
   PFX configuration. Dry-run first; execute real signing only after the DAG
   and policy checks pass. Derive engine/service pins from the exact signed
   artifacts and prove any rebuild/re-sign fixed point.
6. Generate and validate the portable kit. Require an exact `package/`
   inventory: four product executables, V2 manifest, and detached signature.
7. If and only if signed-package, pin, CMS, Administrator, and clean-host
   prerequisites pass, run the canonical privileged driver. Separately observe
   install/service identity, mutual authentication/heartbeat, MFT, USN changes
   and resume, restart/reconnect, rollback/upgrade fixtures, uninstall, and
   cleanup. Import evidence only through the canonical importer.
8. Attempt direct rendered UI validation with repository-supported desktop/UIA
   tools when available. Do not substitute headless tests or textual claims for
   screenshots/interactive evidence.
9. Repair and regression-test any repository-owned defect found by the real
   path. Re-run affected gates, update sanitized evidence and truthful OpenSpec
   task state, run final hygiene/CI checks, commit, push, and inspect the run
   whose `head_sha` is the pushed SHA.

## Validation matrix

| ID | Gate | Required evidence | Current status |
|---|---|---|---|
| PV-01 | Administrator capability | Explicit Windows token role/integrity result | PASS |
| PV-02 | Release artifact integrity | Pre-handoff unsigned release-test candidate was x64/version/build-identity correct; final live candidate provenance is derived after the last pushed SHA; production Release remains pin-blocked | PASS (historical candidate checkpoint) |
| PV-03 | Authorized production signer availability | Explicit approved store thumbprint or external PFX provider; policy-valid chain/private key | BLOCKED_EXTERNAL |
| PV-04 | Production signing | Dry-run plus real signed/verified artifacts and timestamp status | BLOCKED_EXTERNAL |
| PV-05 | Production leaf-pin provenance | Pins derived from exact final Engine/Service signer leaves | BLOCKED_EXTERNAL |
| PV-06 | Package V2 / detached manifest signature | Canonical final manifest, exact hashes, valid authorized CMS signature | BLOCKED_EXTERNAL |
| PV-07 | Clean-host preflight | Read-only canonical preflight passes before installation | BLOCKED_ENVIRONMENT |
| PV-08 | Elevated installer transaction | Signed package install, confinement, ACL, exact payload, metadata | BLOCKED_EXTERNAL |
| PV-09 | Service identity/security | LocalSystem broker, exact image, SCM/task/group/ACL policy | BLOCKED_EXTERNAL |
| PV-10 | Engine/service authenticated connection | Mutual Authenticode/image-path pins, Active state, heartbeat revalidation | BLOCKED_EXTERNAL |
| PV-11 | MFT initial scan | Real authenticated raw-volume/MFT records through parser/store/projection/snapshot | BLOCKED_EXTERNAL |
| PV-12 | USN/resume/incremental update | Distinct journal-open, create/rename/modify/delete, cursor/resume evidence | BLOCKED_EXTERNAL |
| PV-13 | Restart/reconnect/recovery | Exact-process restart, reauth, snapshot remap, durable index, USN resume | BLOCKED_EXTERNAL |
| PV-14 | Upgrade/rollback | Two genuinely signed identities, upgrade, forced rollback, downgrade rejection | BLOCKED_EXTERNAL |
| PV-15 | Uninstall/cleanup | Canonical uninstall, exact FastFiles-resource cleanup, no unrelated deletion | BLOCKED_EXTERNAL |
| PV-16 | Evidence import/validation | Canonical importer accepts a structurally valid PASS bundle | BLOCKED_EXTERNAL |
| PV-17 | Direct rendered UI | Interactive screenshot matrix and real aggregate-backed percentage assertion | BLOCKED_ENVIRONMENT |
| PV-18 | Final source/CI/handoff review for prior repair slice | Tests/hygiene/OpenSpec and hosted jobs for the independently reviewed `8c369dcd13693c2cc1c85d86c7ae23054a3ae3e1` slice | PASS (historical; derive live on resume) |
| PV-19 | Local signer-hardening handoff | Certificate policy, timestamp policy, real WinTrust expiry fixtures, diagnostic-bypass gate, focused tests, final clean push and hosted jobs by final SHA | NOT VERIFIED; real WinTrust fixture is blocked before trusted acceptance |

| PV-BUG-01 | Encrypted external PFX loading | Secure password is consumed without prompting or secret output | PASS |
| PV-BUG-02 | PE/manifest signing sequencing | Final manifest is signed separately from final PE bytes; unsafe combined mode rejected | PASS |
| PV-BUG-03 | Artifact-root pin provenance | Readiness report reads pins from the artifact root under inspection | PASS |
| PV-BUG-04 | Readiness overall status | Report can reach PASS only when all release prerequisites pass | PASS |
| PV-BUG-05 | Production certificate policy | Trusted, currently valid Code Signing leaf with private key for signing; detached CMS uses the same default technical policy; tests isolate technical validity only through a private custom-root seam; revocation status is explicit | PASS |
| PV-BUG-06 | Stale production-validation frontier | Prior pushed/green slice and current signer-hardening slice are separated in durable state without self-referential current SHA | PASS |
| PV-BUG-07 | Placeholder expected-thumbprint parsing | All-zero expected signer values fail closed through explicit boolean thumbprint validation | PASS |
| PV-BUG-08 | PowerShell policy-object pipeline contamination | Certificate-policy collection mutations cannot replace the policy object in detached CMS validation; exact-signer CMS regression uses the private non-production custom-root seam | PASS |
| PV-BUG-09 | Expired WinTrust fixture timing | Disposable expired-signer fixture leaves enough time for signing before the deliberate expiry transition | PASS |
| PV-BUG-10 | Signing/timestamp longevity policy conflation | Signing preflight remains current-valid/trusted/private-key strict; signed PE verification respects a valid WinTrust RFC3161 timestamp; production PE and detached CMS signing require an explicit timestamp URL; final PE/CMS verification checks timestamp state and exact signer provenance | PASS |
| PV-BUG-11 | Confounded runtime expiry negative test | The real fixture attempts to trust only its unique public root in `CurrentUser\Root` through the supported `certutil -user -f -addstore Root` path, then requires exact-pin acceptance before checking the unchanged no-timestamp PE after expiry through production WinTrust; a separate timestamped PE case covers longevity and parent-process cleanup. The current host blocks the trust-store operation before the required acceptance. | BLOCKED_ENVIRONMENT / NOT VERIFIED |

Additional `PV-BUG-*` rows are never relabeled as external merely because they
require an elevated or signed path to reproduce.

## Validation commands

Resume and Git reconciliation use `git fetch --prune origin`,
`git status --short`, `git branch --show-current`, `git rev-parse HEAD`,
`git rev-parse origin/main`, and `git log --oneline --decorate -20`.

The token probe is:

`$identity = [Security.Principal.WindowsIdentity]::GetCurrent()`;
`$principal = [Security.Principal.WindowsPrincipal]::new($identity)`;
`$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)`.

Readiness uses
`pwsh ./verify/release/ReleaseReadiness.ps1 -Mode report -OutputFormat json`.
Use the active Visual Studio developer environment and repository presets for
candidate builds/tests. Use the release-tooling test suite after any
release-script change. Never read or glob the prohibited execution/signing
directories named by `AGENTS.md`.

The hosted run for the first signer-hardening push exposed a fixture-only
failure: opening the default .NET CurrentUser Root view stalled on the runner's
protected-root state. Both trust-store-mutating fixture scripts now use
`certutil -user -f -addstore Root` for the exact generated public root, check
the thumbprint-derived CurrentUser registry key without enumerating the
protected-root view, and remove only that key. On the current development host,
certutil still times out and the focused engine/timestamp/package tests fail
before their required trusted-acceptance assertions; exact cleanup checks pass.
The final full suites and hosted run remain required before handoff.

## OpenSpec task boundary

The remaining tasks in `close-independent-validation-gaps` are 4.2, 4.5, 4.6,
4.7, 4.8, and 5.1. The local clauses of 4.2 are evidenced by
`evidence/authentication-negative-tests.md`: unsigned, mismatched,
placeholder, partially populated, untrusted, and diagnostic-bypass coverage
pass; timestamp-longevity coverage passes in the release-tooling/custom-root
matrix. The real trusted pre-expiry/expired transition remains
`BLOCKED_ENVIRONMENT / NOT VERIFIED`. Task 4.2 remains open for that runtime
evidence and the external release-pipeline signer/pin provisioning clause;
4.5–4.8 require signed/elevated/clean-host evidence and 5.1 requires direct
rendered interaction plus a real aggregate-backed drill-down. The recovery
change has no remaining open tasks.

## Handoff rule

Before ending, update this plan and the paired
`.agent/workstate/fastfiles-production-validation-state.md` with safe summaries,
run `git diff --check`, commit all session-owned tracked changes, fetch and
reconcile, push `main` without force, verify local/remote equality, and inspect
the hosted run matching the final pushed SHA. Report live final SHA and CI in
the response rather than writing a self-referential current-HEAD claim here.
The next authorized action after this local slice is to obtain the approved
production signer/pin and timestamp-service configuration and run the release
DryRun; no production certificate is selected by this campaign.
