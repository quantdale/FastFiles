## Why

PR #10 is open at `9c0ad128f7bfeeef5bc0d66a390c0a8786c024a1` with two unresolved review threads, and the current source can lose retry intent or trap keyboard focus. The predecessor change is complete in its own ledger; these defects are a new closure, not a reason to reopen finished work or to merge while hosted CI cannot execute.

## What Changes

- Require a deterministic red reproducer before editing retry ownership. The reproducer must show that `OnScanFailed` can clear `scanInProgress` and release `mutex_` before `ArmFailedScanRetry` installs an owner, and that the scheduler can erase a due retry into a local vector before that retry is adopted.
- Require exactly one owner for each durable volume's retry intent while it moves among the active attempt, the ephemeral retry queue, scheduler dispatch, and the durable reconnect floor. A disconnect in any of those windows must transfer the intent, not drop it.
- Preserve the existing five-second floor, one outstanding retry per volume, pause/disable/disappearance behavior, fresh ephemeral ids after reconnect, and shutdown clearing. Do not add a tight reconnect loop.
- Require navigation chrome Tab traversal to be enterable from the shell and escapable in both directions. Forward Tab from the last chrome control and Shift+Tab from the first must return focus to the shell owner. They must not wrap inside chrome.
- Adversarially review the existing protocol 2.3 `ScanAttemptStarted` barrier and the new retry owner together. Do not bump the protocol or add a client command unless that review proves an invariant break that the current reply-only message cannot express.
- After production source changes, certify a new exact implementation SHA from scratch. Historical candidates, including `2d470134` and `9c0ad128`, are not evidence for newer source.
- Keep PR #10 unmerged. Local parity is not hosted CI. The raw-volume synchronous `DeviceIoControl` completion bound stays `UNPROVEN`.

## Capabilities

### New Capabilities

- `navigation-chrome-keyboard-traversal`: keyboard users can enter navigation chrome and leave it again, in both directions, without a chrome-only focus cycle.
- `exact-head-certification-hold`: local certification is bound to the implementation candidate that produced the binaries; hosted CI, merge eligibility, and the raw-volume completion bound stay separately classified.

### Modified Capabilities

- `index-engine`: a failed or interrupted scan's retry intent cannot pass through an unowned window between session state, the ephemeral queue, scheduler dispatch, and the durable reconnect floor.
- `privileged-index-service`: the existing `ScanAttemptStarted` reply stays on the closed service-to-engine surface. Its payload, stream order, and authority rules must fail closed under the adversarial cases below. No new client command.

## Impact

- Engine retry ownership in `VolumeSessionManager` (`OnScanFailed`, `OnScanComplete`, `OnConnectionStateChanged`, `ArmFailedScanRetry`, `ArmScanReissue`, `ClearFailedScanRetry`, `TakeScanReissueFloorLocked`, `ReconciliationSchedulerLoop`, `TriggerReconciliation`, `StartOrResumeVolume`). The elevation boundary remains the named-pipe broker in `establish-architecture-foundation` design D4: no generic open-path or open-handle command, and no forwarding of file content.
- Service scan supersede order in `ServiceConnection::StartVolumeScan` and engine dispatch in `PrivilegedConnection`. Protocol 2.3 is already current (`kCurrentProtocolVersion {2, 3}`). A bump is not selected here.
- Navigation chrome Tab handling in `NavigationChrome::HandleTabNavigation` and the shell message loop in `WindowShell::RunMessageLoop`. The shell is not a dialog and does not call `IsDialogMessage`, so returning false at the boundary does not by itself move focus into Column View or Home.
- Tests in `tests/engine/test_volume_session_manager.cpp`, protocol/service adversarial coverage, and a headless chrome-traversal seam comparable to `ffui::AddressEditClosedFocusTarget`. UI Automation and visual runs are certification evidence, not a substitute for the deterministic reproducer.
- PR #10 review threads on `VolumeSessionManager.cpp` and `NavigationChrome.cpp`. GitHub Actions billing, branch protection, signing pins, and trust stores are observed, not edited.
- Predecessor tasks that stay open elsewhere: `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3.

## Goals

- Retry intent for a durable volume has exactly one owner at every transition the two review races can hit, and at the admission, pause, disable, disappearance, and shutdown edges named in the prompt.
- An already-indexed reconciliation that loses its retry does not fall through to only the ordinary reconciliation interval.
- A keyboard-only user can Tab into chrome and Tab or Shift+Tab back to the shell content surface.
- Protocol 2.3 is either proven by adversarial tests or corrected without a speculative bump.
- The certified executable embeds the new candidate SHA. Older certified SHAs are not reused.

## Non-goals

- Implementing the fix in the proposal session.
- Merging PR #10, force-pushing, or rewriting the feature branch.
- Reopening legacy false `ScanComplete` fallback, raw partial-frame injection, provenance-tree digest, many-tabs logical count, unrelated UI redesign, prior transport hardening, or prior governance work without a new reproducer.
- Redesigning raw-volume cancellation, overlapped FSCTL, or helper-process isolation.
- Changing GitHub billing, spending limits, account plan, branch rules, production signing, or trust-store configuration.
- Calling local CI parity, a zero-step hosted run, or a documentation tip hosted or production certification.
- Marking the raw-volume completion bound proven.

## Open Questions

- Does the protocol 2.3 review find an ordering or authority break that the current exact-length `ScanAttemptStarted` message cannot express? Until it does, no protocol bump is in scope.
- Which certification-relevant binaries embed identity, and which of those must be rebuilt after the production-source commits? The exact-head ladder answers this; it is not assumed from `9c0ad128`.
