## Context

See `proposal.md` for why this change exists. Live read-only baseline at proposal time: branch `feat/scan-generation-integrity-and-residual-certification-closure`, HEAD and `origin` tip `9c0ad128f7bfeeef5bc0d66a390c0a8786c024a1`, `origin/main` `1172c1a1e6aa9c1a726d52abdade0975b6979e4b`, feature branch 24 ahead and 0 behind `origin/main`, clean tracked tree, PR #10 open and unmerged. Local `main` is 3 commits ahead of `origin/main`; that is not this branch and must not be merged into it. Two review threads are unresolved and not outdated.

The predecessor change already transfers a *queued* retry onto `scanReissueFloors_` when disconnect holds both `mutex_` and `wakeMutex_`. That transfer is not atomic with the paths that create or dequeue the retry. This design closes those windows. It does not replace the five-second floor or the protocol 2.3 barrier.

## Goals / Non-Goals

**Goals:**

- Make every retry transition install the next owner before another thread can observe none.
- Give the focus cycle a headless decision the existing shell owner can execute.
- Add adversarial tests around the barrier that already exists. Change protocol code only where a test fails.
- Bind later certification to the implementation SHA, not to `9c0ad128`.

**Non-Goals:**

- A new wire message, a resized terminal, or a client open-path command, unless task 4 proves the current barrier cannot express the invariant.
- Holding retry locks across snapshot publication, pipeline I/O, or pipe writes.
- Using `IsDialogMessage` or a new content HWND. The shell is not a dialog.
- Merging local `main`, force-pushing, or treating hosted billing as a product defect.

## Decisions

### D1: Reproduce the failure race through the existing snapshot callback

**Why:** `OnScanFailed` clears `scanInProgress`, releases `mutex_`, and only then calls `RepublishSnapshot(..., force=true)` before `ArmFailedScanRetry`. The snapshot callback therefore runs in the real unowned window. `TestFailedReconciliationRetrySurvivesDisconnect` arms the retry to completion before disconnect, so it does not cover this interleaving.

**Choice:** The red test sets `onSnapshotReady_` to disconnect the manager, then delivers `ScanFailed` for an already-indexed reconciliation. It must fail on the current source by observing no ephemeral retry and no durable floor. Do not sleep. Do not edit the production transition until that test fails.

**Alternative considered:** A new hook between the clear and the arm. Rejected as the first reproducer because the publication callback is already in the window. A hook is still required for the scheduler window in D3, where no callback exists.

### D2: One transition API, both locks, durable identity

**Why:** Retry intent is currently split across `scanInProgress`, `failedScanRetries_` (ephemeral id), a scheduler-local vector, and `scanReissueFloors_` (durable id). `OnConnectionStateChanged` sets `active_ = false` before taking locks, and `ArmFailedScanRetry` returns when `active_` is already false. Those are three unowned windows, not one:

```text
ACTIVE_ATTEMPT
    │  ScanFailed clears flag, releases mutex_
    ▼
  (none)  ◀── disconnect snapshots here and clears the session
    │  ArmFailedScanRetry sees inactive_ and returns
    ▼
  lost

EPHEMERAL_QUEUED
    │  scheduler erases into a local vector, releases wakeMutex_
    ▼
  (none)  ◀── disconnect sees no scan and no queue entry
    │  session is gone, so the scheduler does not re-arm
    ▼
  lost

ArmFailedScanRetry passed the active_ check
    │  disconnect clears the queue, then the arm inserts the dead ephemeral id
    ▼
  stranded id that reconnect never reads
```

**Choice:** Add one owner record per durable volume, moved only by a transition API that holds `scoped_lock(mutex_, wakeMutex_)` in that order. Legal owners:

```text
NONE ──shutdown / authoritative completion──▶ NONE

ACTIVE_ATTEMPT ──failure, connection still current──▶ EPHEMERAL_QUEUED
ACTIVE_ATTEMPT ──failure or disconnect, session ending──▶ DURABLE_FLOOR

EPHEMERAL_QUEUED ──due──▶ SCHEDULER_DISPATCHING
EPHEMERAL_QUEUED ──disconnect or disappearance──▶ DURABLE_FLOOR

SCHEDULER_DISPATCHING ──admitted──▶ ACTIVE_ATTEMPT
SCHEDULER_DISPATCHING ──paused, disabled, or busy──▶ EPHEMERAL_QUEUED
SCHEDULER_DISPATCHING ──disconnect, disappearance, or dead session──▶ DURABLE_FLOOR

DURABLE_FLOOR ──new ephemeral id on an authenticated session──▶ EPHEMERAL_QUEUED
```

`SCHEDULER_DISPATCHING` must be visible to disconnect. A local vector is not an owner. `ArmFailedScanRetry` and `ArmScanReissue` must not decide from `active_` outside the lock. Inside the lock they either queue on the live ephemeral id or floor the durable id. Shutdown is the only transition to `NONE` that discards owed work. Authoritative completion may also go to `NONE` because the intent is satisfied, not lost.

Disappearance in `OnVolumeList` must use the same transfer as disconnect before the ephemeral id is erased. A disabled or paused volume keeps one non-issuing owner and is not scanned. Reconnect consumes the floor once onto the new id and preserves the reconciliation-versus-initial kind. Due times stay on the existing five-second clock; tests move the deadline directly.

**Alternatives considered:**

- Extra `if (!active_)` checks around the current maps. Rejected. The review asked for one owner, and the late-insert window is caused by checking `active_` outside the lock.
- Keying the floor only by ephemeral id. Rejected. That id is invalid after disconnect, which is the bug.
- Taking the locks across `RepublishSnapshot` or `SendRequest`. Rejected. The snapshot callback and pipe write re-enter or block. Install the next owner, release the locks, then publish or send.

This tightens the predecessor's queued-retry transfer. It does not supersede that change's five-second floor or its `ScanAttemptStarted` decision.

### D3: Scheduler seam pauses only after the dispatching owner is visible

**Why:** The due-retry loop erases `failedScanRetries_` and releases `wakeMutex_` before it looks up the session. Nothing in that gap calls back into test code today.

**Choice:** After D2, the scheduler marks `SCHEDULER_DISPATCHING` under the lock pair, releases the locks, then invokes a null-by-default test seam, then admits or re-queues. The red test points the seam at disconnect and must fail before D2 because the current erase leaves no owner. The green test uses the same seam and must observe a floor or a replacement slot. The seam must not run while `mutex_` or `wakeMutex_` is held.

**Alternative considered:** Sleeping until the scheduler thread reaches the erase. Rejected. It is not the transition the review named, and it can pass without owning the intent.

### D4: Escape chrome by focusing the existing shell owner

**Why:** `NavigationChrome::HandleTabNavigation` builds a vector of visible enabled chrome HWNDs and wraps at both ends. `WindowShell::RunMessageLoop` consumes Tab only when that function returns true. It does not call `IsDialogMessage`. Column View and Home are custom-rendered on the shell owner, not Win32 tab stops. Returning false at the boundary therefore does not restore content focus.

**Choice:** Add a pure seam beside `ffui::AddressEditClosedFocusTarget`, for example `ffui::NextChromeTabTarget`. Inputs are the already-filtered chrome vector, the focused HWND, the owner, and the direction. Outputs are the next HWND and whether the caller should consume the key.

- Focus on the owner, or on no chrome control: enter the first control, or the last if reverse.
- Forward from the last control, or reverse from the first: target the owner when `IsWindow(owner)` is true. Do not wrap.
- Otherwise: move to the adjacent chrome control.
- `HandleTabNavigation` calls `SetFocus` on that target and returns whether focus landed there.

Address editing keeps today's early return so the edit is not stolen. Disabled buttons and hidden overflow stay out of the vector through the existing `IsWindowEnabled` / `IsWindowVisible` filters. Overflow activation does not need a second focus target; after it, the same boundary rule still escapes.

**Alternatives considered:**

- Wrap less often but still inside chrome. Rejected. That preserves the trap.
- Return false and rely on dialog traversal. Rejected. The message loop has no dialog traversal.
- Invent a Column View child HWND as the Tab target. Rejected. The shell owner already routes content keys, and address-edit close already returns there.

### D5: Protocol 2.3 is reviewed in place

**Why:** `kCurrentProtocolVersion` is already `{2, 3}`. `ServiceConnection` joins the old worker, writes exact-length `ScanAttemptStarted`, and only then starts the replacement. `PrivilegedConnection` rejects a wrong payload size. The engine ignores a barrier unless that session is in progress and awaiting it. A pre-2.3 coverage-hole replacement resets the connection. The client allowlist already rejects the message as a request. None of that has been proved against the adversarial list in the privileged-index-service delta.

**Choice:** Add those tests against the production parser, service order, and engine authority path. Do not add message 24, and do not widen `ScanAttemptStarted`, unless a failing test shows the current payload cannot preserve ordering or authority. A failed barrier write must still skip the replacement worker and tear down the connection. Authentication generation rules stay the existing connection generation: a barrier from an unauthenticated, stale, or replaced generation must not move the current session.

**Alternative considered:** A protocol bump in the proposal. Rejected. The prompt forbids a bump without an invariant break, and the current message is the ordering barrier the predecessor selected.

### D6: Certification names the post-fix implementation SHA

**Why:** `9c0ad128` does not contain the ownership or focus fixes. Embedding it in new binaries would certify the wrong tree.

**Choice:** Commit the production and test fixes first. Record that SHA only after `git status` is clean. Rebuild every identity-bearing executable from that SHA, then run the local ladder in the proposal. Evidence files and handoff notes come after executable qualification, not before. Hosted CI is inspected once after a normal push. Zero-step job failures are `BLOCKED_EXTERNAL`. PR #10 stays unmerged. Predecessor tasks 5.4 and 2.3 stay unchecked.

**Alternative considered:** Certifying the proposal SHA, or reusing the predecessor's `2d470134` package. Rejected. Those trees do not contain this fix.

## Risks / Trade-offs

- [Risk] A snapshot callback that disconnects re-enters the manager. → Mitigation: D2 installs the owner before publication and never holds the retry locks across the callback. The red test is allowed to re-enter only on the current, unlocked window.
- [Risk] The dispatching seam deadlocks if it runs under `wakeMutex_`. → Mitigation: D3 releases both locks before the seam. Disconnect then takes them and must see the dispatching owner.
- [Risk] Collapse of two maps into one record accidentally changes the five-second floor or creates two passes. → Mitigation: keep one record per durable id, consume the floor exactly once, and extend the existing retry tests rather than replacing them.
- [Risk] `SetFocus(owner_)` runs while a modal menu from overflow is open. → Mitigation: escape applies to chrome controls, not to a menu that already has its own dismiss path. The overflow scenario must activate the hidden tab and then escape from chrome afterward.
- [Risk] Adversarial review finds a real barrier hole and tempts a speculative message. → Mitigation: D5 stops at the failing test. A bump requires that test plus a design note; it is not pre-approved.
- [Risk] Project-status integrity fails if this change has open tasks and no lifecycle entry. → Mitigation: register it `active` when the tasks land, with evidence paths to these artifacts. Do not mark it completed in the proposal session.
- [Risk] Local `main` being ahead of `origin/main` is mistaken for this branch's base. → Mitigation: rebase or merge only if the user later asks. This change stays on the PR #10 branch and on `origin/main`.

## Migration Plan

No store migration and no protocol migration while D5 holds. Ship the fix on the existing feature branch in the commit split from the proposal: retry reproducer plus owner, focus reproducer plus escape, protocol corrections only if required, then certification evidence. Rollback is a normal revert of those commits. Do not reset the branch and do not merge to `main`.

## Open Questions

None that change the specs, the owner model, or the task split. Whether D5's tests fail is an implementation result, not a reason to select a bump now.
