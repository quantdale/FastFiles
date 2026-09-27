# Campaign handoff report A–J — `legacy-protocol-correctness-and-raw-volume-cancellation-closure`

Executable candidate: `9b012bfa689206439181a87cba42c96dbad0579e` (`9b012bf`).
This report is committed as a **documentation tip**; it does not rebuild binaries and is
not a newly bound executable candidate (design D7). Every validation claim below belongs
to `9b012bf`; the tip commit that carries this file is labeled separately.

## A. Repository and integration state

| Fact | Value |
|---|---|
| Feature branch | `feat/legacy-protocol-correctness-and-raw-volume-cancellation-closure` (created from the propose tip `cfaa226`, no reset) |
| Product commits | `e248563` (legacy-failure correction), `9b012bf` (adversarial-review fixes) |
| Documentation commits | `cfaa226` (propose), `cba220a` (pre-existing vendor-name drift fix), this handoff tip |
| Base | `main` at `9566f1c` was `origin/main` when the branch was created; the branch tip was pushed without force and the PR is the review surface (merge waits for hosted checks) |
| Worktrees | the pre-existing `.kilo/worktrees/scythe-cloud` (detached `0dc3356`) untouched; the temporary pre-change reproduction worktree used for the visual baseline was removed |
| Concurrent activity | pre-existing installed `FastFilesIndexSvc` left running and untouched; no development engine/UI residue (E2E and visual runs cleaned up) |
| Lifecycle | this change is the sole `active` campaign; `final-residual-hardening-and-hosted-ci-recovery` is `superseded`; `index-durability-false-success-closure` stays `completed` |

## B. Legacy protocol

**Defect, proven red-first.** The reproducer
`tests/engine/test_legacy_scan_failure_binding.cpp`
(`ffengine_legacy_scan_failure_binding_tests`) drives the real service terminal
decision (`RunVolumeScan`, `RunSyntheticVolumeScanForTesting`, production
`RunScanWorkerBoundary`) into the real engine dispatch
(`PrivilegedConnection::DispatchReceivedFrame` → `VolumeSessionManager` →
`IndexPipeline`) over an in-process pipe. Against the uncorrected implementation it
produced **36 failed / 84 passed checks** — 12 for each of `{1,5}`, `{2,0}`, `{2,1}`,
none for `{2,2}` — with a failed scan delivered as legacy `ScanComplete`, the volume
marked complete, and an in-flight reconciliation pass finalized over a partial
seen-set that deleted a durable row. Red/green numbers and the full failure list are
recorded in `evidence/legacy-scan-failure-reproducer.md`.

**Correction.** `SendScanTerminal` now returns `ScanTerminalOutcome`; a failure the
negotiated peer cannot parse yields `FailureNotRepresentable` (nothing written),
mapped to `ScanRunResult::FailureAbortedConnection`
(`ScanRunResultRequiresConnectionAbort`), which `RunScanWorkerBoundary` turns into a
connection abort with the distinct
`scan-failure-not-representable-connection-abort` diagnostic. The engine discards
in-flight reconciliation passes when the connection is lost. Successful-scan bytes
are byte-identical (`RunLegacySuccessTerminalBytesCase`), negotiation stays on the
documented matrix, `{2,2}` keeps `ScanFailed`, and the closed command surface,
authentication and connection-scoped identities are unchanged.

**Post-review additions (all with reproducers).** A durable per-volume reissue floor
restores the five-second failed-scan cadence for the abort path
(`ScanReissueFloorLocked`/`ArmScanReissue`, `TestLegacyFailureReissueIsThrottledAfterConnectionLoss`);
a batch dropped while paused now makes the in-flight scan non-authoritative
(`TestPausedReconciliationDropIsNonAuthoritative`); a stop that races a pre-enumeration
setting failure stays terminal-free (`TestStopDuringSetupFailureStaysSilent`).

**Residuals.** A malformed trailing enumeration record can be skipped while the cursor
advances (`VolumeScanner.cpp`, Windows-generated buffers, reachability UNVERIFIED) and a
volume that disappears from the poll can leave a reconciliation pass until the next
terminal; both are pre-existing, demonstrated to cause no false success, and recorded
rather than changed speculatively.

## C. Raw-volume cancellation

`evidence/raw-volume-ioctl-inventory.md` enumerates all nine synchronous raw-volume
`DeviceIoControl` sites (scanner and journal), their owning threads, and the six
teardown entry points that end in an unbounded `join()`. The hermetic `StallFixture`
(`tests/indexsvc/test_volume_scanner_terminal.cpp`, inside the ASan slice) parks a
worker in a non-cancelable wait; `evidence/raw-volume-cancellation-architecture.md`
records each guarantee separately and classifies it as **measured**, **derived from
code order**, or **structurally derived**, with explicit UNPROVEN markers.

Selected architecture: keep request + join with truthful reporting (design D5(d)).
Measured: the stop request returns immediately (in the parked non-I/O case
`CancelSynchronousIo` reports `ERROR_NOT_FOUND`; the real pipe-I/O cancellation is
measured separately), the control path is still parked 400 ms after the request, the
worker stops 15 ms after the parked operation returns, cleanup requires kernel
completion, cancellation wins over a setup failure, and a late worker writes nothing
on a stopped or torn-down connection. Derived/UNPROVEN: the service-shutdown bound
(no listener-level parked fixture), replacement-session immunity (structural plus the
write-suppression half measured), and overlapped/`CancelIoEx` support per FSCTL
(requires an elevated live volume — the external frontier). The bounded-admission
quarantine and isolated-helper architectures are recorded as `FUTURE_PRODUCT_WORK`.
Inherited residual 2.3 and this campaign's 5.4 remain **unchecked**: no finite
completion bound is proven for an arbitrary synchronous raw-volume `DeviceIoControl`.
Journal lifecycle coverage exists; the raw `FSCTL_READ_USN_JOURNAL` cancellation path
is UNPROVEN.

## D. Hosted CI

- Workflow topology is **4 job definitions → 6 executions** (`build` × {debug,
  release-test, analyze}, `asan-protocol-ipc-lifecycle`, `repository-hygiene-and-contracts`,
  `unsigned-release-candidate-qualification`), matching the real job list.
- Latest hosted run `36261023218` for `origin/main` tip `9566f1c` failed all six jobs in
  1–6 s with **no step logs** (`gh run view --log-failed` reports no log) and the
  verbatim annotation: *"The job was not started because recent account payments have
  failed or your spending limit needs to be increased."*
- **`HOSTED_CI = BLOCKED_EXTERNAL`.** Unlock: the repository owner clears the account
  billing/spending limit; the next push then triggers the six executions, which must be
  inspected for the exact SHA. No workflow was weakened and no known-unstartable
  workflow was dispatched; the feature-branch push was made once and its automatic run
  was inspected once (result recorded in the session handoff and the PR).
- Local parity (`verify/Invoke-CiParity.ps1`) is labeled
  `LOCAL-PARITY-NOT-HOSTED-CI` and is **not** hosted certification.

## E. Branch protection

`quantdale/FastFiles` is **private** on a plan without branch-protection/ruleset
support. All three read-only API probes return HTTP **403**
(`"Upgrade to GitHub Pro or make this repository public to enable this feature."`):
`branches/main/protection`, `rulesets`, `rules/branches/main`. No protection or ruleset
can therefore be observed or enabled, and nothing was changed. Owner action to enable
the proposed policy (require a pull request plus the six checks for `main`, with review
for privileged-service changes): make the repository public or upgrade the plan, then
create the ruleset. Merge readiness is reported as waiting on that and on hosted checks;
the campaign did not weaken or alter repository permissions or billing.

## F. Validation on `9b012bf`

| Gate | Result |
|---|---|
| Builds | debug, release-test, analyze (`/analyze`), asan, debug-diagnostic — all clean, no warnings |
| Debug CTest | **123/123 passed** (268 s) |
| release-test CTest | **123/123 passed** (244 s) |
| ASan slice (hosted topology: fuzz + framing + listener lifecycle + scanner terminal) | **4/4 passed** |
| Analyze | warning-clean build of every target |
| Strict OpenSpec | **86/86 passed** (`--all --strict --no-interactive`) |
| Provenance / governance / drift / identity | **11/11 passed** (incl. `ffbuild_identity_binding_tests`, `ffdoc_drift_tests`) |
| Release-candidate qualification | `Test-ReleaseCandidate.ps1` — **0 failures** |
| Local CI parity | **PASS 11 / 0 / 0** — `verify/runs/ci-parity/20260927-135033` (local only) |
| E2E (real `FastFiles.exe`, engine-connected) | **8 passed / 0 failed / 0 skipped** (`build/e2e-artifacts/20260927-140422-0d4200d1`), provenance `boundToHead=true` |
| UIA aggregate | **22 passed / 6 skipped with recorded reasons / 0 failed** |
| Visual capability | **1 failed scenario** (`shell-many-tabs-overflow`, tab count 4 < 8) with 14 captures passing — **reproduced identically on the pre-change `cfaa226` build** in a temporary worktree, so it is pre-existing and not caused by this change (narrow-window tab overflow / synthetic-click reachability in an untouched UI area) |
| Focused protocol/service/engine/IPC | `ffprotocol_tests`, `ffprotocol_fuzz_tests`, `ffcommand_surface_tests`, `ffvolumescanner_terminal_tests`, `ffengine_legacy_scan_failure_binding_tests`, `ffengine_volume_session_manager_tests`, `ffengine_index_pipeline_tests`, `ffindexstore_*`, `ffengine_seeded_indexed_mode_tests`, `ffipc_*`, `ffservice_revalidation_policy_tests`, `ffservice_exception_boundary_policy_tests` — all pass |

## G. Adversarial review

Five independent read-only reviewers (tasks 8.1–8.5) challenged the change; every
confirmed material finding was fixed with a reproducer (8.6):

| Finding | Severity | Disposition |
|---|---|---|
| A-F1 / C-MAJOR: unthrottled reconnect→rescan→teardown loop for a persistently failing legacy scan (contradicted "bounded retry preserved") | MAJOR | **Fixed**: durable per-volume reissue floor + bounded retry + `TestLegacyFailureReissueIsThrottledAfterConnectionLoss` |
| A-F2: a batch dropped while paused could still finalize a reconciliation pass after resume and delete valid rows | MAJOR | **Fixed**: paused drop marks the scan non-authoritative + `TestPausedReconciliationDropIsNonAuthoritative` |
| B-MAJOR: the cancellation fixture did not cover journal operations | MAJOR | **Fixed**: `TestJournalWorkerStopUsesTheSameBoundedRequest`; raw journal FSCTL marked UNPROVEN |
| B-MINOR: measurement table entries were inferences; one contradicted the inventory | MINOR | **Fixed**: reclassified measured / derived / structural, UNPROVEN tags |
| B-MINOR: cancellation could still emit a failure terminal on setup paths | MINOR | **Fixed**: stop wins + `TestStopDuringSetupFailureStaysSilent` |
| B-MINOR: seam-policy assertion no longer proved its claim | MINOR | **Fixed**: exact two-target allowlist assertion |
| B-MINOR: supersede fixture read its worker vector concurrently | MINOR | **Fixed**: owner-exclusive vector with atomics |
| E-MINOR-1: stale legacy-fallback comments | MINOR | **Fixed** (`Commands.h`, `Version.h`, test header, `tests/indexsvc/CMakeLists.txt`) |
| E-MINOR-3: abort-path recovery bound undocumented | MINOR | **Fixed**: reissue floor + architecture note |
| D-1/D-2: identity binding across docs tips / targeted builds | MAJOR (process) | **Addressed**: full rebuild of every preset at the candidate, identity-binding test green; full ASan build; tip labeled documentation-only; targeted-build identity-refresh gap recorded as pre-existing build-infra residual |
| D-3: hosted Actions absent from `external-frontier.json` | MINOR | **Recorded** in §H (the inventory predates this campaign and was not silently extended) |
| D-4/D-5/D-7: citation drift, host-privilege-dependent drive letter, undated old log | MINOR | **Fixed/addressed**: citations corrected, unmounted-letter resolution added, the listener-lifecycle suite re-run green in the 7.x ladder |

Report-only residuals (no invariant violation demonstrated): connection-scoped blast
radius when a legacy failure aborts the connection (design trade-off, no data loss);
malformed trailing enumeration record (above); volume-disappearance pass leak;
pre-existing post-stop journal frames; `CancelSynchronousIo` may also abort a
diagnostics write; batch-frame byte identity asserted by message type only (the batch
writers are untouched).

## H. External certification

`verify/certification/external-frontier.json` was rechecked: 25 tasks, every referenced
execution script still present; groups physical-environment 7 / external-provider 8 /
production-signing 5 / privileged-ntfs 2 / clean-host 3, all `REQUIRED-BUT-UNAVAILABLE`
or `BLOCKED_ENVIRONMENT`. The inventory does **not** carry a hosted-Actions boundary, so
it is recorded here explicitly:

| Boundary | Prerequisite | Canonical command | Expected evidence | Success criterion | Unlock action |
|---|---|---|---|---|---|
| Hosted GitHub Actions | Owner billing/spending limit cleared | push to the branch/PR (the workflow triggers) | six executions with steps, logs, artifacts for the exact SHA | all six required executions PASS | clear account billing; inspect the run for the pushed SHA |
| Production signing + pins | Authorized signer/pins + supported trust chain | `pwsh -File verify/certification/Invoke-SigningCertification.ps1 -CandidatePath <candidate> -SignerThumbprint <thumbprint>` | valid Authenticode chain, pin match, unsigned rejection | `laneStatus==PASS` | provision authorized signer/pins |
| Clean-host install | Uncontaminated supported host + elevation | `pwsh -File verify/certification/Invoke-CleanHostCertification.ps1 -InstallCandidate -CandidatePath <candidate>` | conflict-free install/run/uninstall, zero residue | all legs pass | provide a clean host and elevation |
| Elevated live MFT/USN | Elevated NTFS host + disposable fixture | `pwsh -File verify/certification/Invoke-PrivilegedNtfsCertification.ps1 -VolumeRoot C: -DiagnosticFixtureDirectory <dir>` | read-only MFT access, parsed USN records, per-field match | all fields match | authorize the privileged campaign |
| Physical display matrix | Attached desktop with requested DPI/contrast | `pwsh -File verify/certification/Invoke-UiaLongRunCertification.ps1 -Scenario visual-matrix …` | requested dimensions realized, zero assertion failures | PASS across dimensions | provide the physical display configuration |
| Disposable guest | Authorized isolated provider + lawful image | `pwsh -File verify/certification/Invoke-EnvironmentProviderCertification.ps1 -Provider <vendor-specific VM provider id>` | verified preflight, guest provenance, clean teardown | PASS | authorize provider execution |

No production trust material was provisioned, no privileged install or live MFT/USN
run was performed, and no billing or permission change was made.

## I. Remaining work, classified

- `LOCAL_ACTIONABLE`:
  - `shell-many-tabs-overflow` visual scenario (pre-existing; product/harness
    investigation at narrow widths) — reproduced on the pre-change build;
  - targeted-build identity refresh (`cmake/FastFilesVersion.cmake` registers the
    identity generator as an ALL-only target, so hosted/parity `--target` builds do not
    refresh it) — pre-existing build-infra residual, full builds used here;
  - publication of the pushed-SHA hosted result into the handoff/PR (single inspection).
- `BLOCKED_EXTERNAL`: hosted CI (billing); production signing/pins; clean-host install;
  elevated live MFT/USN; physical DPI / High Contrast / mixed-monitor; disposable guest;
  branch-protection/ruleset enablement (private plan).
- `UNPROVEN`: raw-volume kernel completion bound (inherited 2.3 / this change's 5.4);
  journal `FSCTL_READ_USN_JOURNAL` cancellation bound; service-shutdown bound while a
  worker is parked; replacement-session immunity beyond the measured write suppression;
  malformed-trailing-record reachability.
- `SPECULATIVE`: none implemented.
- `FUTURE_PRODUCT_WORK`: overlapped `DeviceIoControl` + `CancelIoEx` per FSCTL (after a
  live-volume measurement); heap-owned connection context + capped worker quarantine for
  bounded control-plane return; isolated helper process (only if in-process containment
  proves insufficient).

## J. Verdicts (stated separately)

- **Local implementation:** complete for this campaign's scope; all product and test
  work is on the feature branch.
- **Local certification:** **PASS on `9b012bf`** — Debug 123/123, release-test 123/123,
  ASan 4/4, Analyze clean, strict OpenSpec 86/86, governance/provenance 11/11,
  release-candidate qualification clean, local CI parity 11/0/0, E2E 8/8, UIA 22/6/0 —
  with the one pre-existing, change-independent visual-scenario failure disclosed.
- **Hosted CI:** **BLOCKED_EXTERNAL** — no hosted certification, and local parity is not
  a substitute.
- **Integration / merge readiness:** branch pushed without force and a reviewable PR
  prepared; merge waits on hosted checks (and, for the privileged-service content, on
  the branch-protection policy the account cannot currently enable).
- **Privileged runtime certification:** **not certified** — no elevation, pins, live
  MFT/USN, or clean-host evidence was produced.
- **Production release certification:** **not certified** — the production-signed,
  clean-host, externally validated path remains owner-authorized work only.
