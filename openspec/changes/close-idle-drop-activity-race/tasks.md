## 1. IdleManager epoch

- [ ] 1.1 Add a drop generation (or `dropCancelled_`) under `IdleManager`'s mutex. Snapshot it when entering the unlocked idle callback; after the callback, call the drop only if the generation is unchanged (design D1).
- [ ] 1.2 Keep store maintenance / VACUUM outside the mutex. Do not hold `mutex_` across `onIdleTimeout`.
- [ ] 1.3 Preserve the existing NotifyActivity → `onReconnectNeeded` path for a drop that already completed (design D2).

## 2. Tests

- [ ] 2.1 Add `ffengine_idle_manager_tests` linking `src/engine/src/IdleManager.cpp`. The idle callback MUST block on a test event so `NotifyActivity` during the unlocked window is deterministic (design D3).
- [ ] 2.2 Assert activity-during-callback does not invoke Drop (or invokes reconnect after); assert quiet idle still invokes the timeout callback; assert activity after a completed drop invokes reconnect.
- [ ] 2.3 Register the target in `tests/engine/CMakeLists.txt` and run `ctest --test-dir build/debug -R ffengine_idle_manager --output-on-failure`.
