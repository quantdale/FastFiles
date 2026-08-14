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
   generation, and final package validation. This audit repaired encrypted-PFX
   loading, stale manifest sequencing, wrong-root pin provenance, and a
   permanently blocked readiness status; deterministic regression coverage is
   in `Test-ReleaseTooling.ps1`.
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
| PV-02 | Release artifact integrity | Current unsigned release-test candidate is current x64/version/build identity and exact four-file inventory; production Release remains pin-blocked | PASS (candidate only) |
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
| PV-18 | Final source/CI/handoff review | Tests/hygiene/OpenSpec, clean push, hosted jobs by pushed SHA | VALIDATING |

| PV-BUG-01 | Encrypted external PFX loading | Secure password is consumed without prompting or secret output | PASS |
| PV-BUG-02 | PE/manifest signing sequencing | Final manifest is signed separately from final PE bytes; unsafe combined mode rejected | PASS |
| PV-BUG-03 | Artifact-root pin provenance | Readiness report reads pins from the artifact root under inspection | PASS |
| PV-BUG-04 | Readiness overall status | Report can reach PASS only when all release prerequisites pass | PASS |

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

## OpenSpec task boundary

At campaign start, the remaining tasks in
`close-independent-validation-gaps` are 4.2, 4.5, 4.6, 4.7, 4.8, and 5.1.
Tasks 4.5–4.8 require signed/elevated/clean-host evidence; task 5.1 requires
direct rendered interaction and a real aggregate-backed drill-down. Task 4.2
also remains open while expired/untrusted signer fixtures and release-pipeline
evidence are absent. The recovery change has no remaining open tasks.

## Handoff rule

Before ending, update this plan and the paired
`.agent/workstate/fastfiles-production-validation-state.md` with safe summaries,
run `git diff --check`, commit all session-owned tracked changes, fetch and
reconcile, push `main` without force, verify local/remote equality, and inspect
the hosted run matching the final pushed SHA. Report live final SHA and CI in
the response rather than writing a self-referential current-HEAD claim here.
