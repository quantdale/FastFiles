# Final residual hardening and hosted CI recovery — handoff report

This report records local qualification before the final push. The pushed SHA
and its hosted result are live handoff facts; the session response will report
those after the push, without editing tracked state to chase CI metadata.

## A. Repository state

- Branch: `main`; working tree was clean before this report and task checkpoint.
- Executable candidate: `12fb0fb649fe307e5a51ea24811b60aab717b8e2`, tree
  `761832b4f08684af0f3da71f9e3cbc58cfb9be69`.
- Documentation/runtime-validation tip: `54cbe9211a9e9d73af762183af5c292d6e2fbd29`,
  tree `b2f1dd731c1e9db5ef7a6de6c4f8ec2976647fa7`.
- Last fetched `origin/main`: `1b7d1c4a30431a8a171483b5958bb92f5c1daf9a`;
  local branch was 10 commits ahead and 0 behind. Fetch again immediately
  before pushing.
- The existing detached `.kilo/worktrees/scythe-cloud` remains untouched.
  The pre-existing `FastFilesIndexSvc` process PID 16216 remains untouched.

## B. Partial-frame transport proof

The test-only IPC write seam emits a requested real named-pipe byte prefix,
then fails the following overlapped write. Focused fixtures cover zero bytes,
partial and complete headers, partial payload, and all-but-final-byte output;
the production frame reader observes the emitted bytes and rejects incomplete
frames. Scanner fixtures also exercise production-shared batch serialization,
terminal decisions, connection-read cancellation, no later terminal after a
partial write, and the worker exception boundary.

Task 2.3 remains open. `CancelSynchronousIo` requests cancellation, but the
scanner and journal worker can be inside a synchronous raw-volume
`DeviceIoControl`; no finite completion bound is proven, and joining can wait
for that call to return.

## C. Provenance decision

The evidence-backed source-tree digest decision is **NOT JUSTIFIED**. The
existing gate binds required binaries to the claimed commit identity and
checks hashes, PE architecture, version resources, package consistency, and
guarded release-kit contents. `boundToTree` is explicitly
`false` / `NOT_EMBEDDED`; this is not a source-tree digest, build-input
attestation, or reproducible-build proof. The detailed decision is in
`evidence/provenance-digest-decision.md`.

## D. Hosted CI

The last observed baseline run was `36059336384` for `1b7d1c4`. All six jobs
failed before executing steps, produced zero artifacts, and carried the
billing/spending-limit annotation. That is an external availability result,
not a code or test result. The final pushed SHA still needs one live status
check; do not manually repeat a dispatch if GitHub again rejects jobs before
execution.

## E. Validation

- `Test-ReleaseCandidate.ps1 -BuildRoot .\build\release-test`: passed on exact
  candidate `12fb0fb`.
- `Invoke-CiParity.ps1` on that same candidate: **11 PASS / 0 FAIL / 0 SKIP**,
  including Debug and release-test full CTest, Analyze, ASan 4/4, strict
  OpenSpec, repository policy/hygiene, drift, and unsigned release-candidate
  qualification.
- UIA aggregate on the diagnostic runtime tip: **22 passed, 6 skipped,
  0 failed**. A focused drag-out repeat was classified as skipped when the
  TeraCopy shell chooser covered Explorer's drop point; its screenshot and
  structured subresult are retained under ignored `build/` output. The earlier
  aggregate's drag-out failure was not reproduced.
- Real E2E on `54cbe92`: **8/8 passed, 0 skipped**; binary provenance was
  bound to HEAD and clean, with no embedded tree hash.
- Core visual catalog: **16/16 passed**. Gallery-inclusive catalog:
  **20/20 passed**. Both had clean teardown.
- `Test-Provenance.ps1`: passed; strict OpenSpec: **85/85**; documentation
  drift: **1/1**.

## F. Adversarial review

Independent transport, provenance, and scope/security reviews found no
remaining blocking defect in the locally testable paths. The certification
review identified the candidate's child-JSON output and archive-authority
contract as findings; both were corrected and covered by release regressions.
The final claims preserve the distinction between commit binding and tree
binding. The open raw-volume completion bound remains explicit in task 2.3.

## G. External certification inventory

The canonical inventory is `verify/certification/external-frontier.json`; it
records prerequisites, commands, evidence requirements, success predicates,
and dispositions. The relevant remaining lanes are:

| Lane | Prerequisite and command | Evidence and success condition | Current classification |
|---|---|---|---|
| Production signer and pins | Authorized production signer/pins and supported Windows trust chain; `pwsh -File verify/certification/Invoke-SigningCertification.ps1 -CandidatePath <candidate> -SignerThumbprint <thumbprint>` | Valid Authenticode chain, pin match, unsigned rejection; `laneStatus==PASS` | `REQUIRED-BUT-UNAVAILABLE`; no signing or trust-store provisioning performed |
| Clean-host install | Uncontaminated supported Windows host and elevated install authorization; `pwsh -File verify/certification/Invoke-CleanHostCertification.ps1 -InstallCandidate -CandidatePath <candidate>` | Conflict-free install/run/uninstall and zero residue; all legs pass | `REQUIRED-BUT-UNAVAILABLE` |
| Privileged MFT/USN | Elevated supported NTFS host with raw-volume access and a disposable diagnostic fixture; `pwsh -File verify/certification/Invoke-PrivilegedNtfsCertification.ps1 -VolumeRoot C: -DiagnosticFixtureDirectory <dir>` | Read-only MFT access, parsed USN records, per-field fixture match | `BLOCKED_ENVIRONMENT`; not run under this task's authority |
| DPI, contrast, and physical display | Attached desktop supporting the requested DPI and High Contrast dimensions; `pwsh -File verify/certification/Invoke-UiaLongRunCertification.ps1 -Scenario visual-matrix -Preset debug-diagnostic -Iterations 25 -Theme Light,Dark -DpiPercent 100,150 -Contrast Normal,HighContrast -Viewport Narrow,Normal,Wide -PaneTopology Single,Dual` | All requested dimensions realized with zero assertion failures | `BLOCKED_ENVIRONMENT`; this host is 125% / 120 DPI |
| Disposable guest/provider | Authorized isolated provider and lawful clean reference image; `pwsh -File verify/certification/Invoke-EnvironmentProviderCertification.ps1 -Provider hyperv` | Verified preflight, guest provenance, clean teardown | `REQUIRED-BUT-UNAVAILABLE`; vendor-specific guest execution remains owner-excluded |

## H. Classified remaining work

- Local: finite completion behavior for synchronous raw-volume IOCTLs remains
  unproven (task 2.3).
- Hosted: inspect the automatic run for the final pushed SHA; if it is
  billing-blocked, retain that exact annotation and stop repeated dispatches.
- External: production signing/pins, elevated privileged scan, clean-host
  install, and physical DPI/contrast/display qualification remain gated by
  their listed environments. Disposable guest execution remains excluded by
  the owner's scope directive.
- Runtime: the current full UIA aggregate is green with recorded skips. The
  focused TeraCopy obstruction is an environment skip; the earlier single
  drag-out failure remains unreproduced, not erased.

## I. Verdict

The unsigned local executable candidate is qualified by exact-SHA local CI
parity, provenance/release regressions, UIA, E2E, and visual coverage. This is
not production-release or privileged-path certification. Task 2.3 and the
external certification and hosted-availability gates remain open.
