# Certification residuals and handoff

Status: local implementation and exact-candidate validation are complete.
Reviewable PR #10 is open with an explicit no-merge hold. The single post-push
hosted recheck completed, but no hosted job reached a workflow step. This
report records the external and inherited limits; it does not claim hosted
certification.

## Raw-volume completion bound — UNPROVEN

The arbitrary synchronous raw-volume `DeviceIoControl` completion bound is
**UNPROVEN**. `CancelSynchronousIo` requests cancellation of a worker's
synchronous operation, but it does not provide a completion deadline.
`StopAndJoin` and `StopAndJoinAll` still end in unbounded `std::thread::join()`
calls, so supersede, disconnect, and service shutdown can wait for the kernel
operation to return. The prior campaign's simulated stall and pipe-I/O
cancellation tests do not prove a finite bound for raw volume or USN journal
control calls.

Source inventory and limits: `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/evidence/raw-volume-ioctl-inventory.md` and `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/evidence/raw-volume-cancellation-architecture.md`.

Keep `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task
5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 unchecked.
Do not mark the predecessor complete or superseded on the strength of this
campaign.

## Hosted CI and merge hold

Apply-start run `36302493823` belongs to SHA
`1172c1a1e6aa9c1a726d52abdade0975b6979e4b`: all six jobs failed before any
step because account billing prevented job start. No job executed. The
protection and ruleset API reads returned HTTP 403 on the private free plan;
the protection state therefore remains unknown. Neither fact grants permission
to merge.

After the feature branch was pushed, reviewable PR
[#10](https://github.com/quantdale/FastFiles/pull/10) was opened with a
NO-MERGE HOLD. The one inspected post-push run was
[`36414059606`](https://github.com/quantdale/FastFiles/actions/runs/36414059606),
event `pull_request`, head `1f5c267b19b0ef80db5a6112308ea95dfa7e5dae`. All six
required jobs (`debug`, `release-test`, `analyze`, `asan-protocol-ipc-lifecycle`,
`repository-hygiene-and-contracts`, and
`unsigned-release-candidate-qualification`) completed as failures before any
step; each job reported an empty step list, so none of the hosted workflow ran.
The queried post-push job records carried no additional failure summary. The
apply-start run remains the recorded billing annotation; do not invent a more
specific cause for this run. No workflow was manually dispatched or rerun.
The pushed SHA is source-equivalent to the exact executable candidate below.

Do not dispatch another copy of those jobs. Keep PR #10 reviewable with its
explicit no-merge hold while the required hosted jobs cannot start. Local CI
parity must be reported as local evidence, never as hosted CI.

## Interactive-environment limits

- An earlier full UI Automation run had one `drag-out-to-explorer` failure
  because the user's `NAYEON_16: StudyMaker` window occluded the Explorer drop
  point. On the final candidate, the UIA core run passed 16/16 and the full run
  passed 23 scenarios, skipped 6, and failed 0. Leave the unrelated foreground
  window untouched; the later run is the current result.
- The available desktop reports 120 DPI / 125% scaling, which was covered by
  the live catalog. Live 100%, 150%, and 200% DPI variants were unavailable.
- No production signing pins, certificate provisioning, trust-store mutation,
  elevated service installation, or privileged live MFT/USN validation is in
  scope for this closure. The degraded/unprivileged path remains the ordinary
  validation path.

## Candidate-specific local evidence

### Executable identity and source equivalence

- Executable candidate: commit `2d470134442f180601b4b508ff7c911c7d124313`.
- Diagnostic UI binary: `build/debug-diagnostic/src/ui/FastFiles.exe`;
  `PrivateBuild` is the full candidate SHA above. SHA-256 is
  `7ed64a7a19cc19b692f7fc3f55fec30267237c3743aaf0a71642e028d431e9e3`.
- The final candidate's `cmake`, `src`, `tests`, and `verify` trees are
  source-equivalent to clean parity candidate
  `a06ca9d3d87c9c521302f47e5b97af56090efaed`; the compared Git paths have no
  content delta. This binds the local build/test matrix below to the same
  executable source while keeping documentation-only checkpoints explicit.

### Local build, test, provenance, and governance results

| Validation | Revision / command | Result |
| --- | --- | --- |
| Full local CI parity | `a06ca9d3d87c9c521302f47e5b97af56090efaed`; `pwsh ./verify/Invoke-CiParity.ps1` | 11 PASS, 0 FAIL, 0 SKIP. This is `LOCAL-PARITY-NOT-HOSTED-CI`; it included policy, strict OpenSpec, Debug/release-test/Analyze builds, CTest, ASan, hygiene/exact-tree and unsigned release-candidate qualification. |
| Debug full CTest | clean source-equivalent executable revision `65be2aee19671170f91f7ffe4657e1337a2f2dee`; discovered CTest with `--test-dir build/debug --output-on-failure` | 123/123 passed, 0 failed, 0 skipped. |
| Release-test full CTest | same source-equivalent revision; discovered CTest with `--test-dir build/release-test --output-on-failure` | 123/123 passed, 0 failed, 0 skipped. |
| Static analysis | `b34233a3d6356f65614457d42ef41fb7b5bbb451`; `cmake --preset analyze` then `cmake --build --preset analyze` | Configure and build passed; warning-clean. |
| Targeted ASan | `b34233a3d6356f65614457d42ef41fb7b5bbb451`; six protocol, IPC, scanner-terminal, scan-manager, and legacy-binding test targets | 6/6 passed. |
| Isolated build identity | candidate `bac5d616070ec6accade403273501c43d44747e9`; `evidence/targeted-build-identity-isolated.ps1` | Eight identity-bearing targeted builds passed A→B independently; six targeted ASan binaries were confirmed to have no source-revision carrier. `ffbuild_identity_freshness_tests` rejects empty/malformed overrides, and the well-formed override was embedded unchanged. |
| Binary identity binding | `0e4d456085db7f7d58e6ec369960bb974c8b8006`; Debug and release-test binding targets | Both passed 1/1 after rebuilding all five version-resource executables and the package-trust test to that identity. |
| Focused regressions | `b9b29112820bfb77765d33b2bc173a146eaa9d55`; volume-session-manager, package-trust, build-identity-freshness, and binary-identity-binding tests | 4/4 passed. |
| Final documentation, provenance, and lifecycle gates | Current candidate documentation; `openspec validate --all --strict --no-interactive`; `verify/check_source_markers.ps1`; `verify/check_diagnostic_policy.ps1`; `ffdoc_drift_tests`, `ffsource_provenance_fixture_tests`, and `ffproject_status_ps_tests` | OpenSpec 87/87; source markers PASS across 235 production files; diagnostic policy 0 failures; focused CTest 3/3; project-status lifecycle model PASS. |

### Final-candidate interactive evidence

| Command | Candidate / binary identity | Result |
| --- | --- | --- |
| `pwsh ./dev.ps1 test-e2e` | commit and diagnostic `PrivateBuild` `2d470134442f180601b4b508ff7c911c7d124313` | 8 passed, 0 failed, 0 skipped; `boundToHead=true`, `sourceClean=true`. |
| `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=core` | same exact candidate | 16 passed, 0 failed, 0 skipped. |
| `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=all` | same exact candidate | 23 passed, 6 skipped, 0 failed. The runner reported skip reasons; they are not expanded from protected run artifacts. |
| `pwsh ./verify/visual/Invoke-VisualCatalog.ps1 -Gallery` | same exact candidate | 20 passed, 0 failed, 0 skipped/blocked (16 core plus 4 gallery scenarios). This live run covered 120 DPI / 125% scale; unavailable DPI variants remain external. |
| `pwsh ./verify/verify.ps1 run -Capability windows-ui-visual-validation -CapabilityOption VisualScenario=shell-many-tabs-overflow` | same exact candidate; verifier launched in an isolated child PowerShell with console output captured outside the repository | Exit 0; 1 passed, 0 failed, 0 skipped/blocked. The strengthened scenario checked logical tab count, normal/narrow overflow, keyboard selection, and workspace identity after close and switching. An earlier foreground invocation ended with `-1073741510` before producing a scenario result; no matching application-error event was found, and the isolated full rerun passed. |

Visual catalog outputs generated under the separate
`final-product-completion-and-production-readiness` evidence directory were
restored/removed after recording the 20/20 result; no unrelated campaign
artifacts are included in this change.

### Remaining external results

- Live 100%, 150%, and 200% DPI variants remain unavailable on this host.
- Hosted CI run `36302493823` on apply-start SHA
  `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` failed all six jobs before any
  step due to account billing. The single post-push check was run on the
  source-equivalent pushed candidate and failed all six jobs before any step;
  its result and the PR no-merge hold are explicit above.
- Production signing pins, certificate provisioning, trust-store mutation,
  elevated service installation, and privileged live MFT/USN validation remain
  outside this closure. The synchronous raw-volume completion bound remains
  **UNPROVEN**; predecessor task 5.4 and residual task 2.3 remain unchecked.
