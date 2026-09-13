# Implementation and validation — close-idle-drop-activity-race

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- `IdleManager` now separates idle maintenance from the actual drop: `Start`
  takes `onIdleTimeout` (VACUUM-class work, runs outside the mutex) and
  `onIdleDrop` (the privileged-connection drop). A per-activity generation
  (`activityGeneration_`) is snapshotted when the idle decision is made; the
  drop runs only when no activity arrived during the maintenance callback
  (design D1).
- The drop is tracked with `dropInFlight_`/`reconnectPending_`: activity that
  lands while the drop callback itself is running defers its reconnect until
  the drop returns, so a reconnect can never be issued before (and then undone
  by) the drop. Activity after a completed drop still reconnects immediately
  through the existing `NotifyActivity → onReconnectNeeded` path (design D2).
- `src/engine/src/Main.cpp` wiring updated: VACUUM in the maintenance callback,
  `DropForIdle` and `RequestReconnect` as the drop/reconnect callbacks.

## Files

- `src/engine/src/IdleManager.{h,cpp}`
- `src/engine/src/Main.cpp`
- `tests/engine/test_idle_manager.cpp` (new)
- `tests/engine/CMakeLists.txt`

## Tests added (`ffengine_idle_manager_tests`)

- Quiet idle runs maintenance, drops, and reconnects on later activity.
- Activity during the unlocked maintenance callback cancels the drop and
  requests reconnect (deterministic via a test-blocked callback).
- Activity while the drop callback is in flight defers reconnect until after
  the drop, then reconnects.
- `FF_IDLE_MANAGER_TESTING` compresses the monitor cadence to 10 ms; the
  production binary keeps the 15 s interval.

## Validation

- `ctest --test-dir build/debug -R ffengine_idle_manager --output-on-failure`:
  `ffengine_idle_manager_tests` PASS.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
