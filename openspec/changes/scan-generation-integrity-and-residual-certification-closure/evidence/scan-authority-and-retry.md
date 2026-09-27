# Scan-authority and interrupted-reconciliation retry (tasks 2.1–5.2)

Revision under test: `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` plus this
change's working-tree edits. Build: `cmake --preset debug` (Ninja, MSVC
`/W4 /WX`), target `ffengine_volume_session_manager_tests`.

## Task 2.1 — the reproducer (Finding A)

`TestResumeDoesNotClearCoverageHoleForAnOlderCompletion`
(`tests/engine/test_volume_session_manager.cpp`) drives the reported order on
one connection-scoped volume id through the production callbacks:

1. an incomplete initial scan applies a batch (cursor `{1,2,3}`);
2. indexing is paused and a later batch (cursor `{4,5,6}`) is dropped, which
   records the coverage hole and leaves the cursor frozen;
3. indexing is resumed, which issues replacement work (`StartVolumeScan`) for
   the *same* connection-scoped volume id;
4. the **original** attempt's `ScanComplete`, already written to the pipe, is
   delivered before the replacement finishes.

The test's own phases are the only evidence that distinguishes the two
attempts, because the wire terminal carries only the volume id — exactly the
condition design D2 records.

## Task 2.2 — the already-indexed reconciliation form

`TestPausedReconciliationOlderCompletionAfterResumeDeletesNothing` runs the
same pause-then-terminal order against an already-indexed volume whose
reconciliation pass is in flight, and asserts the pass is discarded (never
finalized on a partial seen-set), the cadence clock is untouched, and no
durable row is deleted.

**Source-backed impossibility proof for the unsafe variant.** For the
already-indexed form, `resume` cannot clear the hole before the older terminal
is applied:

- `VolumeSessionManager::StartOrResumeVolume` only grants a fresh authority
  slate (and only touches `scanBatchFailed`) inside the
  `!meta->scanComplete` branch. An already-indexed volume never enters it, so
  resume issues no scan for it and the recorded hole survives the resume.
- The only other replacement issuer, `TriggerReconciliation`, defers while
  `scanInProgress` is true and returns without clearing anything; a pass
  exists only while its own scan is in flight
  (`TriggerReconciliation` sets both together, and every terminal clears
  both).
- A reconciliation scan is always issued with an **empty** cursor
  (`BuildStartScanPayload(..., {}, kStartVolumeScanLowPriority)`), so a later
  replacement reconciliation re-enumerates the whole MFT rather than resuming
  past a dropped window.

So the unsafe order in the already-indexed reconciliation shape is unreachable,
and the test pins the safe observable behaviour instead. The red baseline log
confirms it: no assertion in that test failed before the fix.

## Red baseline (before any production edit)

The two source files were reverted to `HEAD` and the test file kept, so the
suite ran the new probes against the pre-fix engine
(`build/_red_scan_authority.log`, copied to
`evidence/scan-authority-red-baseline.log`):

```text
exit=1
132 ok
24 FAIL
```

Key failures for the reported orders:

- `FAIL: the older completion never marks the volume complete` (2.1) — the
  stale terminal authorized the replacement.
- `FAIL: the retired attempt's stale terminal grants no completion` and
  `FAIL: the replacement's own terminal is not the authority while the hole is
  owed` (5.1) — same gap through the combined sequence.
- `FAIL: the durable intent is consumed exactly once on transfer`,
  `FAIL: the durable intent moves onto exactly one slot for the new volume id`,
  `FAIL: the retry addresses the new connection-scoped id, never the discarded
  one`, `FAIL: an expired floor issues one reconciliation retry promptly
  through the retry slot`, `FAIL: one outstanding retry per volume` — Finding B
  (an interrupted reconciliation never entered the five-second retry queue;
  an already-indexed reconnect never consulted the floor at all).
- `FAIL: only the unpaused volume's retry issues a scan`,
  `FAIL: repeated failure stays throttled to the five-second floor` — the
  queue semantics the retry path must preserve.

## Fix implemented (3.1)

All of it stays inside the closed command surface: no new message, no
resized `ScanComplete`/`ScanFailed`, no client-supplied path or handle.

- `VolumeSession::scanUnretiredAttempts` (D2/D3): when a replacement is issued
  for an attempt that is still in flight with an unresolved coverage hole, the
  hole moves into this counter instead of being cleared. The first terminal
  read for the session is consumed as that older attempt's **retirement**: it
  marks nothing, finalizes no pass, and erases no hole. The replacement's own
  terminal is also refused while the hole is owed, so no volume-id-only
  terminal can authorize a replacement issued over a hole; the bounded retry
  reissues the coverage from the last durably applied cursor and *that*
  attempt becomes authoritative. This is the conservative branch of design D2
  — because an ambiguous terminal cannot be attributed on the wire, the engine
  pays one extra (cursor-resuming) attempt instead of ever trusting the wrong
  one.
- A terminal that cannot grant authority for a coverage hole now arms exactly
  one bounded five-second retry (`ArmFailedScanRetry`), so missing coverage is
  repaired without waiting for the multi-minute poll or the six-hour
  reconciliation interval. An authoritative completion clears the slot
  (`ClearFailedScanRetry`).
- `TakeScanReissueFloorLocked` consumes the durable floor exactly once and
  carries its stored kind (initial scan vs. reconciliation). `StartOrResumeVolume`
  now consults it for **already-indexed** volumes too: the interrupted
  reconciliation's intent is transferred onto the replacement connection's own
  ephemeral id at the same durable due time, and its journal is still reopened.
  An already-expired floor still enters the retry queue (promptly) instead of
  waiting for the long interval.
- The scheduler re-arms a due retry while its volume is paused or busy instead
  of dropping it, keeping one slot per volume across connection replacement.

## Task 3.2 — no attempt discriminator message is needed

After 3.1, the campaign's own probes show that a volume-id-only terminal can
no longer authorize a replacement issued over a coverage hole: both the
retired attempt's terminal and the replacement's own terminal are refused, and
only a later attempt whose batches started from the frozen cursor becomes
authoritative (`TestResumeDoesNotClearCoverageHoleForAnOlderCompletion`,
`TestCombinedPauseDisconnectReconnectStaleTerminalRecovers`). Therefore the
conditional fallback (a new negotiated message type) is **not** triggered: no
command, message value, or payload layout changed, and the closed surface in
`openspec/changes/establish-architecture-foundation/design.md` D4 is intact.

## Task 5.1 — combined regression

`TestCombinedPauseDisconnectReconnectStaleTerminalRecovers` runs one sequence:
pause → dropped batch → resume with replacement → stale terminal → repair via
the bounded retry → authoritative completion → reconciliation pass interrupted
by disconnect → reconnect under a new connection-scoped id → stale terminal
for the lost id refused → retry under the new id → authoritative
reconciliation completion, with every durable row still present.

## Task 5.2 — affected suites

```text
ctest --test-dir build/debug -R "ffprotocol|ffipc|ffindexstore|ffengine|ffvolumescanner_terminal"
28 tests, 27 passed; ffengine_volume_session_manager_tests reported Failed once
```

That single failure was a fixture-timeout assertion in the pre-existing
`TestPartialFrameDisconnectReconnectsAndReauthorizes`
(`reconnect service fixture completed both transport generations cleanly`) —
that test's own pipe fixture uses 3 s / 5 s / 8 s windows and is unrelated to
the edited code path. Re-runs: **256/256 checks green** and CTest `Passed`
standalone, then 50 consecutive clean whole-suite executions of
`ffengine_volume_session_manager_tests` (one earlier one-off of the same
fixture assertion in the first 25-run batch; 0 failures in the following 25).
The flake is recorded here as a pre-existing timing bound of that fixture, not
converted into a pass, and the full-suite runs in the campaign's certification
gate resample it.
