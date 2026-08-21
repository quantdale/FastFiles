# Audit area: ENGINE/SERVICE LIFECYCLE &amp; DEGRADED MODE — audit src/ui/src/EngineClient.{h,cpp}, EngineDiscovery.{h,cpp}, EngineStatusPolicy.h, the engine process src/engine/src/ (session lifecycle, ServiceConnection, snapshot publisher), and src/ipc/ framing/listener. Cover: engine discovery robustness, launch-failure handling (e.g. unsigned-engine rejection leaving perpetual Refreshing), handshake/reconnect/backoff logic, heartbeat, active-to-degraded and degraded-to-active transitions, service crash mid-scan, snapshot section remap on new-generation notifications, generation counter safety, orphan/zombie process avoidance, status surface accuracy vs actual state.

(agent agent-6, outcome completed)

## Defects

### [P1] Idle-drop of the privileged connection is instantly undone — connection churn every 10 idle minutes — src/engine/src/PrivilegedConnection.cpp:103
- Symptom (user-visible): after ~10 minutes without browsing activity, the status badge flickers "degraded" and immediately back to "Instant search"; the engine tears down and re-establishes the privileged service connection (handshake, EnumerateVolumes, journal re-open) in a pointless cycle. The intended power/resource-saving idle drop (Main.cpp task 4.10) never actually happens.
- Evidence: `DropForIdle()` does `idleDropped_ = true; SetState(Disconnected, IdleDropped); RequestReconnect();` — but `RequestReconnect()`'s first statement is `idleDropped_ = false;` (PrivilegedConnection.cpp:109-116). `RunLoop` then wakes, sees `idleDropped_ == false`, skips the park-wait at line 605-610, and calls `ConnectVerifyAndHandshake()`. The comment at line 106 ("wakes the loop, which then observes idleDropped_ and waits") describes the opposite of what the code does.
- Failure scenario: UI open, user reads files for 10 min without navigating → `IdleManager::MonitorLoop` fires `onIdleTimeout` → VACUUM attempt + `DropForIdle()` → immediate reconnect loop iteration. Repeats every 10 min.
- Root cause: `RequestReconnect()` conflates "user asked for reconnect" (which legitimately cancels an idle drop) with "wake the loop" used internally by `DropForIdle`.
- Suggested fix: give `DropForIdle` its own wake that does not reset the flag — e.g. add a private `WakeLoop()` that only sets `wakeRequested_`/notifies, and call it from `DropForIdle`; keep the reset inside the public `RequestReconnect()`.
- Regression test idea: engine-level test driving `PrivilegedConnection` with a fake pipe server: after `DropForIdle()`, assert no new `CreateFileW`/Handshake occurs within e.g. 30 s while `idleDropped_`; assert `RequestReconnect()` does end the park.

### [P2] Pending FolderAggregate callbacks are never failed on disconnect — perpetual "computing size" spinner — src/ui/src/EngineClient.cpp:655
- Symptom: if the engine restarts/crashes (or the control pipe drops) while a folder-size aggregate request is in flight, the UI callback for that request never fires; the affected folder's size cell spins/pends forever until the user re-navigates.
- Evidence: after `ReaderLoop()` returns, ManagementLoop runs `ClosePipe(); FailIndexedSearchCallbacks(); invalidationCv_.notify_all(); UnmapSnapshotSection();` (lines 655-658). `FailIndexedSearchCallbacks()` (line 675) drains only `indexedSearchCallbacks_`; `aggregateCallbacks_` (EngineClient.h:178) has no equivalent drain anywhere (grep confirms it is only touched by insert/find/erase-on-reply).
- Failure scenario: expand a large folder (aggregate request sent) → engine exits/is killed → UI reconnects → old request id never answered, entry stays in the map forever (also a slow memory leak of callbacks).
- Root cause: disconnect cleanup was written for indexed search only and never extended to the request-id-keyed aggregate map added later.
- Suggested fix: add `FailFolderAggregateCallbacks()` mirroring `FailIndexedSearchCallbacks()`, invoking each stored callback with `FolderAggregateStatus::NotFound`, and call it next to `FailIndexedSearchCallbacks()` in `ManagementLoop`.
- Regression test idea: UI-side test registering an aggregate callback, forcing a disconnect (close pipe), asserting the callback fires exactly once with a failure status and the map is empty.

### [P2] `IdleManager::NotifyUiWindowOpen` is dead code — the "no UI window" half of the idle condition can never be true — src/engine/src/IdleManager.cpp:53
- Symptom: the privileged connection is dropped after 10 minutes even while a FastFiles window is open and visible (as long as no browse/watcher activity occurred), contradicting Main.cpp:100 ("no UI window **and** no significant recent filesystem-change activity").
- Evidence: grep for `NotifyUiWindowOpen` finds only the declaration and definition — no caller anywhere in `src/`. `anyWindowOpen_` therefore stays `false` forever, so `!anyWindowOpen_` in `MonitorLoop` (IdleManager.cpp:80) is always true.
- Failure scenario: user leaves a window open untouched for 10 min → idle drop fires (and, per the P1 above, immediately reconnects) → avoidable service round-trip, badge flicker, VACUUM attempt during a live session.
- Root cause: the window-open signal was designed but never wired from the UI/engine handshake (there is no UI→engine "window count" message).
- Suggested fix: either wire a real window-count notification (new control message or piggyback on Subscribe/heartbeat from UI), or drop `anyWindowOpen_` from the condition and update the Main.cpp comment to match actual behavior.
- Regression test idea: unit test `IdleManager` directly: with `NotifyUiWindowOpen(true)` and no activity past the timeout, `onIdleTimeout` must not fire.

### [P3] `LaunchEngineIfNotRunning` treats `ERROR_PIPE_BUSY` as "engine not running" — possible duplicate engine launch — src/ui/src/EngineClient.cpp:97
- Symptom: transient extra engine process launch + spurious diagnostics when all pipe instances are momentarily busy; benign today only because the duplicate exits via `FILE_FLAG_FIRST_PIPE_INSTANCE`.
- Evidence: `if (WaitNamedPipeW(pipeName.c_str(), 0)) return;` — a `FALSE` with `GetLastError() == ERROR_PIPE_BUSY` (pipe exists, no free instance) falls through into candidate validation and `CreateProcessW`.
- Failure scenario: two UI windows connect simultaneously, consuming the listener's pending instance between them; second UI sees BUSY and launches a second engine.
- Suggested fix: check `GetLastError()` after the failed `WaitNamedPipeW`; on `ERROR_PIPE_BUSY` treat the engine as running and return.
- Regression test idea: hard to unit-test directly; at minimum assert the guard logic via a extracted helper (`IsPipeReachable(name)` returning tri-state).

### [P3] Frame-read deadline is per-`ReadFile`, not per-frame — a trickling peer can stretch a "5 s" handshake indefinitely — src/ipc/src/PipeFraming.cpp:128
- Symptom: none in normal operation; a stalled/malicious same-privilege peer delivering 1 byte per 4 s keeps `ReadExactOverlapped` alive far beyond the caller's `kHandshakeTimeout`/`kPipeWriteTimeout` deadline, because each chunk gets a fresh full wait (line 159 uses `WaitMilliseconds(deadline)` recomputed per iteration against the *same* absolute deadline — actually correct within one call, but the outer `while (remaining > 0)` loops reset nothing… see below).
- Correction on closer read: the deadline is absolute and shared across iterations, so this specific concern is unfounded for mid-frame reads. Residual nit only: `ReadFrameOverlapped` enforces no minimum-progress guarantee beyond the deadline, which is fine. Downgraded — see Verified sound instead.

### [P3] Finished per-connection listener threads accumulate for the engine's lifetime — src/ipc/src/PipeListener.cpp:162
- Symptom: slow handle/memory growth in the long-lived engine proportional to UI reconnects (each UI reconnect/disconnect cycle leaves a finished `std::thread` object in `connectionThreads_` until `Stop()`).
- Evidence: `connectionThreads_.emplace_back([...] { onConnect_(acceptedConnection); ...erase handle...; });` — the lambda removes its cancellation handle but nothing ever joins or erases the thread entries except `JoinConnectionThreads()`, called only from `Stop()` (line 229).
- Failure scenario: flaky connection causing reconnect cycles every ~10 s for days → thousands of dead thread handles retained.
- Suggested fix: have the connection lambda detach-safe-reap: move its own `std::thread` out via a self-erase under `connectionMutex_` (e.g. store threads in a `std::map<std::thread::id, std::thread>` and have the lambda `join`+erase its own entry after `onConnect_` returns), or periodically reap finished threads in the accept loop.
- Regression test idea: listener test connecting/disconnecting N times, asserting `connectionThreads_` size stays bounded.

### [P3] Failed watch installation is completely silent to the UI — stale listings with no signal — src/engine/src/DirectoryWatcher.cpp:73
- Symptom: if `CreateFileW` for the change-handle fails (common for network shares, some ACL configs) after a successful enumeration, the directory renders once and then silently goes stale — no `DirectoryError`, no diagnostic, unlike every other watcher failure path.
- Evidence: lines 73-76: `if (directoryHandle == INVALID_HANDLE_VALUE) { // e.g. permission-denied ... no replacement is installed }` — no `AppendDiagnostic`, no client notification; contrast with the fully-instrumented failure paths in `WatchLoop`.
- Suggested fix: emit an `FFDIAG_WATCH_*` diagnostic here at minimum; ideally send a non-fatal notice so the UI can badge the column as unwatched.
- Regression test idea: inject a path whose enumeration succeeds but watch-open fails; assert a diagnostic record is produced.

## Verified sound

- **Snapshot generation protocol**: immutable per-generation sections named `<session>.<GUID>.<gen>`, `ERROR_ALREADY_EXISTS` collision rejection, generation committed last via interlocked exchange after full serialization (SnapshotPublisher.cpp:170-194); UI remaps before announcing and keeps the last good mapping if the new section is unusable (EngineClient.cpp:395-400); `announcedGeneration_` is correctly re-seeded from the SubscribeAck on every reconnect, so a restarted engine's small generation numbers are not dropped.
- **UI-side snapshot validation**: format-version, generation-match, payload-size vs `kSnapshotMaximumPayloadBytes` and `VirtualQuery` region-size checks before parse (EngineClient.cpp:47-68); read-only mapping loaded with acquire barrier + volatile load rather than an Interlocked store that would fault (lines 31-40).
- **Bounded reconnect cycle**: 5 attempts × 2 s then parked `Unavailable` until explicit `RetryConnect()` (badge click, wired at WindowShell.cpp:1052) — no infinite silent retry; retry-delay waits are interruptible by Stop.
- **Shutdown race handling**: `ConnectAndSubscribe` refuses to publish a pipe after `Stop()` began; failed writers retire the exact handle under locks; overlapped ops cancelled before joins on both UI and engine sides.
- **IPC framing hardening**: max-frame check in u64 before allocation, structVersion validated, byte-mode short-read loop, bounded quarantined-cancellation design with fail-closed cap (PipeFraming.cpp).
- **Service identity verification**: image-path-under-install-dir + pinned Authenticode thumbprint before handshake; placeholder pins fail closed (`VerifyPinnedSignature`, AuthenticodeVerification.cpp:134); dev unsigned-engine launch rejection is logged with stage-specific diagnostics and surfaces as `Unavailable`, not a hang.
- **Degraded↔active transitions**: ephemeral VolumeIds cleared on every non-Active transition; volume absence marks unavailable (never deletes); reconnect resumes from persisted cursor/journal USN instead of blind rescan; JournalId mismatch and `JournalResumeInvalid` both trigger reconciliation rather than silent gap loss.
- **DirectoryWatcher**: overlapped `lpBytesReturned` correctly taken from `GetOverlappedResult` even on synchronous arm; zero-byte completion treated as buffer overflow with re-enumeration and bounded give-up; terminal errors do one final refresh publish; startup handshake prevents racing first change.
- **Heartbeat**: 20 s interval / 10 s ack timeout, timeout treated as disconnection per spec; reader-thread exceptions caught and converted to disconnect instead of `std::terminate`.

Note: the P1 and the second P2 compound — the idle feature both fires when it shouldn't (window open) and accomplishes nothing when it does fire (instant reconnect), so task 4.10 is effectively broken in both directions despite passing headless tests that presumably exercise the pieces in isolation.
