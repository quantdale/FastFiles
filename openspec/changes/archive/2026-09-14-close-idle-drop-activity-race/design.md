## Context

See proposal.md. `IdleManager::MonitorLoop` sets `droppedForIdle_ = true`, unlocks, runs `onIdleTimeout` (VACUUM then `DropForIdle`), relocks. `NotifyActivity` under the mutex clears the flag and calls `RequestReconnect` outside. `PrivilegedConnection::DropForIdle` sets `idleDropped_` and `WakeLoop` without clearing it; `RequestReconnect` clears `idleDropped_` and wakes. The race is IdleManager's, not the later DropForIdle/RequestReconnect split (that split was already fixed vs an older "immediate reconnect" bug).

## Goals / Non-Goals

**Goals:** Activity during the idle callback cannot stick the privileged path in IdleDropped with reconnect disabled.

**Non-Goals:** UI-window-open conjunct; changing `kCheckInterval` / idle timeout; SnapshotPublisher Stop (PA-022, production never stops).

## Decisions

### D1: Generation token for the in-flight idle drop

Under `mutex_`, increment `dropGeneration_` and snapshot it when deciding to drop. Pass that generation into the callback epoch. `NotifyActivity` increments (or sets a `dropCancelled_` flag) under the same mutex. After the callback, re-lock and call `DropForIdle` only if `dropGeneration_` is unchanged and `droppedForIdle_` is still true. If activity ran, skip `DropForIdle` (connection stays up, or RequestReconnect already ran).

**Why:** VACUUM must remain outside the mutex (it can take seconds). The generation is the standard "cancel this epoch" pattern.

**Rejected:** Holding the mutex across VACUUM — stalls NotifyActivity and UI-thread waiters. Running DropForIdle before VACUUM — still races unless the generation check wraps both.

### D2: If DropForIdle already ran, activity must still reconnect

If the callback completed DropForIdle just before NotifyActivity, `droppedForIdle_` is true and NotifyActivity's existing reconnect path is correct. Tests must cover both orderings: activity-before-drop (D1 skips drop) and drop-before-activity (existing reconnect).

### D3: New `ffengine_idle_manager_tests` target

Link `IdleManager.cpp` only. Inject a callback that waits on a test-controlled event so the test can call `NotifyActivity` during the unlocked window. Fake PrivilegedConnection is unnecessary if IdleManager exposes whether it invoked onIdleTimeout vs onReconnectNeeded.

## Risks / Trade-offs

- **[Risk] Test flakiness if the unlocked window is too small.** → Mitigation: callback blocks on a test event; the test is deterministic, not a sleep race.
- **[Risk] DropForIdle vs RequestReconnect still unsynchronized atomics on PrivilegedConnection.** → Mitigation: out of scope unless a test shows a remaining hole after D1; `idleDropped_` is already `std::atomic`.

## Migration Plan

Engine binary only. No on-disk state.

## Open Questions

None.
