## Why

Foundation design D4's "Standing exposure window" mitigation is that `FastFilesEngine` drops the privileged connection after idle and **re-establishes on the next activity burst**. `IdleManager::MonitorLoop` sets `droppedForIdle_`, unlocks, then runs the production callback (VACUUM then `DropForIdle`) outside the lock (PA-011). `NotifyActivity` during that window clears the flag and `RequestReconnect`s; the stale `DropForIdle` then parks the just-reconnected pipe. Because `droppedForIdle_` is already false, later activity never reconnects while the user is active — the privileged path stays down until the next full idle cycle. There is no IdleManager test target.

## What Changes

- Make idle-drop and reconnect mutually exclusive: a reconnect decided while the idle callback is in flight SHALL win, and `DropForIdle` SHALL NOT run after that reconnect; conversely, a drop that has begun SHALL complete and the next activity SHALL reconnect.
- Keep VACUUM in the idle callback but do not allow it to create a reconnect hole.
- Add a deterministic IdleManager unit test that fires activity during the unlocked callback and asserts the privileged connection is requested back (or never dropped).

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `index-engine`: idle-drop SHALL reconnect on the next activity burst even when activity races the idle callback.

## Impact

- `src/engine/src/IdleManager.{h,cpp}`, production wiring in `src/engine/src/Main.cpp` (callback order vs DropForIdle).
- New `tests/engine` target linking `IdleManager.cpp` (none exists today).
- No change to the closed command protocol, Authenticode pins, or service.
- Non-goals: restoring the never-wired "no UI window open" conjunct; changing idle timeout defaults.

## Open Questions

- None. Generation/token or "drop cancelled" flag under `mutex_` is sufficient; design records D1.
