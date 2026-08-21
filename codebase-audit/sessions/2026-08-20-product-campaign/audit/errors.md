# Audit area: ERROR HANDLING &amp; MESSAGING SWEEP (cross-cutting) — Grep-driven audit across src/ for: ignored return values (HRESULT, GetLastError patterns, error_code, bool results dropped silently), empty catch/else after failures, unbounded retry loops, blocking waits on the UI thread (Sleep, WaitForSingleObject with long timeouts, synchronous IPC on UI thread), MessageBox/modal spam patterns, silent fallbacks hiding degradation, generic 'failed'/'error' strings surfaced to users without actionable content, inconsistent error surfacing between similar situations. Rank findings by user impact; cite file:line for each.

(agent agent-11, outcome completed)

## Defects

### [P1] Pending folder-aggregate requests are never failed on engine disconnect — UI stuck on "Calculating…" forever — src/ui/src/EngineClient.cpp:656
- Symptom (user-visible): Select a folder → Details pane shows `Items: Calculating… / Total size: Calculating…` indefinitely. Storage Analysis drill-down and Treemap tiles stay in "calculating"/pending spinner state. Reselecting the same folder does not recover.
- Evidence: `ManagementLoop()`'s disconnect path calls only `FailIndexedSearchCallbacks();` (EngineClient.cpp:656) — the `aggregateCallbacks_` map (EngineClient.cpp:924) is never drained, unlike indexed-search callbacks which get a synthetic `Unavailable` response (EngineClient.cpp:675-692). The request-sent fallback (`failed(requestId, …NotFound…)`, EngineClient.cpp:945) covers only an immediate write failure, not a mid-flight disconnect.
- Failure scenario: User selects a large folder (aggregate takes seconds); engine restarts or the pipe drops; the aggregate result never arrives. In WindowShell the per-folder gate `FolderAggregateRequestState::NeedsRequest` (`src/ui/include/ffui/DetailsAggregateState.h:29-31`) returns false because the key already matches, so even after reconnect the same folder is never re-requested — the stale "Calculating…" survives until a different folder is selected. Same leak in `StorageAnalysis` (`parentAggregatePending_` stays true, `pending_` entries accumulate, src/ui/src/StorageAnalysis.cpp:614-638) and `TreemapView` (`node.calculating` never clears, src/ui/src/TreemapView.cpp:227-256).
- Root cause hypothesis: Disconnect cleanup was written for the newer indexed-search feature and never extended to the older aggregate callback registry.
- Suggested fix: Add `FailAggregateCallbacks()` mirroring `FailIndexedSearchCallbacks()` (invoke each with `FolderAggregateStatus::Unavailable`/`NotFound`) and call it next to `FailIndexedSearchCallbacks()` in `ManagementLoop` after `ClosePipe()`.
- Regression test idea: UI-level seam test — register an aggregate callback, force `ClosePipe()` + disconnect path, assert the callback fires exactly once with a failure status and `aggregateCallbacks_` is empty.

### [P2] Ignored `GetFileAttributesExW` failure renders 1601-01-01 timestamps in Details pane — src/ui/src/WindowShell.cpp:1404
- Symptom: For a selection whose attributes can't be read (file deleted/raced between snapshot and properties render, or access denied), Created/Modified/Accessed show as `1601-01-01 …` instead of "Unavailable".
- Evidence: `WIN32_FILE_ATTRIBUTE_DATA data{}; GetFileAttributesExW(selection->path.c_str(), GetFileExInfoStandard, &data);` — return value discarded; `data` stays zeroed and flows into `FormatTime(data.ftCreationTime)` etc. (lines 1454-1456). `FormatTime` (WindowShell.cpp:92-96) only returns "Unavailable" when conversion fails, and a zero FILETIME converts fine.
- Failure scenario: Browse a directory where another process deletes a file after the snapshot was published; select it; details show bogus 1601 dates and an empty attribute string.
- Root cause hypothesis: Missing failure branch for the metadata refresh.
- Suggested fix: Capture the BOOL; on failure emit `Created/Modified/Accessed: Unavailable` (and skip the Attributes line).
- Regression test idea: Headless test of the details-string builder with a forced-failing attribute provider asserting "Unavailable".

### [P2] Clipboard copy results silently dropped — copy commands can no-op without any feedback — src/ui/src/WindowShell.cpp:387,399
- Symptom: "Copy path" / "Copy relative path" appear to succeed but the clipboard is unchanged.
- Evidence: `CopyPathsToClipboard(hwnd_, paths);` (line 387) and line 399 discard the BOOL. `CopyPathsToClipboard` → `CopyTextToClipboard` returns false whenever `OpenClipboard` fails (src/ui/src/QuickActions.cpp:76) — a routine transient condition while any clipboard-manager/Office/RDP client holds the clipboard. There is no retry and no MessageBox, in contrast to sibling commands on the same dispatch chain which do surface failures (e.g. lines 383-385 "The item could not be opened.").
- Failure scenario: User copies a path with a clipboard manager running; OpenClipboard loses the race; nothing lands on the clipboard; user pastes stale content elsewhere.
- Root cause hypothesis: Return value ignored; inconsistent with the surrounding command handlers that all report failure.
- Suggested fix: Check the result and show a short MB_ICONWARNING ("The path could not be copied to the clipboard."); optionally retry `OpenClipboard` a few times with ~10 ms delays before giving up.
- Regression test idea: Not directly unit-testable (clipboard contention); cover via a code-review/drift assertion or injectable clipboard seam test asserting the caller reacts to `false`.

### [P3] Diagnostic export failure always reported as "no diagnostic log found" regardless of actual cause — src/ui/src/WindowShell.cpp:2266
- Symptom: When export fails because the destination file couldn't be opened/written (locked by another app, permission denied), the user is told "Export failed: no diagnostic log found." — wrong diagnosis, sends them hunting for a nonexistent log problem.
- Evidence: `ExportDiagnosticBundle` returns a bare `false` from four distinct failure points (missing path/empty dest Diagnostics.cpp:382-384, unreadable source :386-400, output-open failure :472-475, write failure :500); the UI collapses all of them into one message (WindowShell.cpp:2265-2267).
- Suggested fix: Have `ExportDiagnosticBundle` return a reason enum (or out-param `DWORD lastError`) and phrase the message accordingly ("could not write to <path>: 0x…").
- Regression test idea: Protocol test asserting distinct failure reasons for missing-log vs unwritable-destination.

### [P3] Indexing-pause toggle flips local state even when the command never reaches the engine — src/ui/src/SettingsDialog.cpp:559-563
- Symptom: With the engine disconnected/unavailable, toggling "Pause indexing" updates `globalPaused_` (checkbox appears paused) but `SetIndexingPaused` fire-and-forgets a frame that `WriteCurrentFrame` drops (returns false, ignored, EngineClient.cpp:951-954). The subsequent `RefreshVolumeStatuses()` will show unpaused volumes, leaving the dialog internally contradictory; if the status refresh also fails, the checkbox lies outright.
- Evidence: `globalPaused_ = paused; engineClient_->SetIndexingPaused(0, paused);` — no success check; header documents fire-and-forget semantics relying solely on read-back (EngineClient.h:81-84).
- Suggested fix: Only commit `globalPaused_` after the status read-back confirms the paused flag (or revert the checkbox when `WriteCurrentFrame` fails).
- Regression test idea: Seam test: disconnected EngineClient stub whose write fails; assert the dialog's pause checkbox reverts.

### [P3] FileOperations worker dies silently if `CoInitializeEx` fails; queued operations sit in "Queued" forever — src/ui/src/FileOperations.cpp:211
- Symptom: User initiates copy/move/delete; progress shows "Queued" indefinitely; no error, no timeout.
- Evidence: `if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return;` — thread exits before `workerThreadId_ = GetCurrentThreadId()` is set, so `Enqueue`'s `PostThreadMessageW(workerThreadId_, WM_APP_FILE_OPERATION_REQUEST, …)` (line 199) is skipped forever and nothing drains `queue_`. No event is posted for the dead worker.
- Failure scenario: Rare (COM init failure under resource exhaustion or apartment conflicts), but when it hits, every subsequent file operation for the session hangs in Queued with zero feedback.
- Suggested fix: On CoInitializeEx failure, post a `Failed` event (or set a `workerDead_` flag checked in `Enqueue` that immediately completes queued requests with a failure HRESULT).
- Regression test idea: Injected-COM seam test asserting Enqueue after a failed init produces a terminal event rather than silence.

## Verified sound

- **EngineClient handshake/reconnect**: bounded 5-attempt cycle then explicit Unavailable with retry affordance (EngineClient.cpp:25, 626-650); stage-specific diagnostics preserve `GetLastError` across logging (:269-277); Stop() cancels exact handles before joins, no deadlock path (:721-754); double-lock publish prevents post-shutdown pipe publication (:340-352); invalidation queue pops only after the frame is accepted (:786-793).
- **Snapshot mapping**: read-only mapping discipline documented and correct (`LoadSharedU32/U64`, :31-45); generation/payload validation against region size before parse (:47-68); stale-mapping retention on failed remap (:395-398).
- **DirectoryWatcher**: buffer-overflow (zero-byte completion) treated as resync, bounded give-up after 8 consecutive overflows, terminal errors trigger one final refresh callback so state is published before retirement (DirectoryWatcher.cpp:201-240); evicted watches logged; errored watches are replaceable (:57-66).
- **DegradedModeEnumerator**: FindFirstFileEx open errors classified (AccessDenied/NotFound/etc.), partial-iteration `FindNextFileW` failures classified rather than silently truncated (DegradedModeEnumerator.cpp:49-81).
- **Engine startup**: corrupt-DB delete-and-rebuild fallback, non-fatal DB-open failure keeps degraded browsing alive with explicit stderr + diagnostics (engine/Main.cpp:166-198); pinned-signature refusal outside debug (:129-137).
- **Installer service verification**: bounded 30 s wait, crash-on-start settle window, explicit stderr failures (InstallSteps.cpp:562-602).
- **Diagnostic bundle export**: redaction applied per-line for both legacy key/value and JSON streams; literal paths gated behind explicit opt-in (Diagnostics.cpp:444-470).
- **No blocking waits on the UI thread found**: all `Sleep`/`WaitForSingleObject` occurrences are on background threads (engine park loop, installer, service scanner pacing, EngineClient management thread); conflict decisions poll with 100 ms `wait_for` on a worker thread, not the UI thread (FileOperations.cpp:241-245).
