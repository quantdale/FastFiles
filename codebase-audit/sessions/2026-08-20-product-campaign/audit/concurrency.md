# Audit area: CONCURRENCY, LIFETIME &amp; CANCELLATION (cross-cutting) — audit thread/callback/lifetime safety across src/ui/src (WindowShell, Renderer, IconCache, UiAnimation, Preview, EngineClient, ColumnView async paths, OleDragDrop) and src/engine/src. Look for: worker callbacks firing after owner destruction, cross-thread UI access without marshaling, COM apartment discipline (CoInitialize pairing, OLE drag-drop, icon extraction, UIA provider calls), D2D device-loss recovery fanout gaps, snapshot double-buffer publication races, watcher callbacks outliving watched directories, shutdown ordering hazards, rapid open/close/reconnect races, duplicated completion notifications, unbounded thread/handle growth. Report concrete unsafe interleavings with file:line, not generic advice.

(agent agent-8, outcome completed)

## Defects

### [P1] Engine hangs: `ClientConnection::Close()` joins the search worker while holding `writeMutex_` the worker needs — src/engine/src/UiServer.cpp:240
- Symptom (user-visible): FastFilesEngine.exe permanently hangs when the UI disconnects at the moment an indexed search completes; the engine stops serving all UI clients (pipe listener thread wedged in that connection's cleanup), so after reopening the app browsing/search silently never recovers until the engine process is killed.
- Evidence: `UiServer.cpp:239-248`:
  ```cpp
  if (pipe != nullptr) pipeCanceller_(pipe->handle);
  std::lock_guard<std::mutex> writeLock(writeMutex_);
  { ... pipe_.reset(); }
  if (indexedSearchThread_.joinable()) { indexedSearchThread_.join(); }
  ```
  while `IndexedSearchWorker` (`UiServer.cpp:189-207`) snapshots `stopping = indexedSearchStopping_` under `indexedSearchMutex_`, releases it, and then calls `Write(...)` (`UiServer.cpp:205`) whose **first** statement is `std::lock_guard<std::mutex> writeLock(writeMutex_);` (`UiServer.cpp:81`) — before the `closing_` early-out.
- Failure scenario: worker finishes `task.handler` (SearchIndexed), reads `stopping == false`; UI disconnects (or `UiServer::Stop` clears `subscribedClients_`); `Close()` sets `indexedSearchStopping_ = true`, cancels the pipe, acquires `writeMutex_`, calls `join()`. The worker now blocks acquiring `writeMutex_`; `join()` waits for the worker. Classic AB/BA deadlock.
- Root cause hypothesis: the comment "Cancel before waiting for a concurrent writer" covers a writer already *inside* `frameWriter_`, but not the search worker that is about to become one.
- Suggested fix: re-check `closing_`/`indexedSearchStopping_` inside `Write()` **before** taking `writeMutex_` (cheap atomic or stateMutex_ peek), or release `writeMutex_` before `join()` (scope the lock around only the `pipe_.reset()` block).
- Regression test idea: UiServer test with a fake `onRequestIndexedSearch` handler that blocks until released; start a search, call `client->Close()` from another thread, then unblock the handler; assert `Close()` returns within a timeout.

### [P2] Folder-aggregate callbacks are never failed on disconnect — requests hang forever and leak — src/ui/src/EngineClient.cpp:655
- Symptom (user-visible): Details pane shows "Items: Calculating… / Total size: Calculating…" indefinitely; Storage Analysis drill-down rows show "Calculating…" forever; Treemap tiles stay "calculating" — whenever the engine disconnects/restarts while any aggregate request is in flight. `aggregateCallbacks_` also grows monotonically across reconnect cycles.
- Evidence: on disconnect `ManagementLoop` calls only `FailIndexedSearchCallbacks()` (`EngineClient.cpp:656`); `Stop()` (`EngineClient.cpp:721-754`) fails neither map. `aggregateCallbacks_` has no other drain path than a matching reply (`EngineClient.cpp:502-533`). Contrast: indexed-search callbacks get an explicit `Unavailable` synthetic response (`EngineClient.cpp:675-692`).
- Failure scenario: user selects a folder (WindowShell.cpp:1417 sets `Pending`, shows "Calculating…"), engine restarts (idle drop, crash, upgrade) before `FolderAggregateResultV2` arrives → callback entry stays in the map forever, no UI message ever posted.
- Root cause hypothesis: the disconnect-failure fanout was written for indexed search only and never extended to the aggregate/request-response maps added later.
- Suggested fix: add `FailFolderAggregateCallbacks()` mirroring `FailIndexedSearchCallbacks()` (synthesize `FolderAggregateStatus::NotFound`), call it next to `FailIndexedSearchCallbacks()` in `ManagementLoop` and in `Stop()`.
- Regression test idea: EngineClient-level test with a mock pipe: issue `RequestFolderAggregate`, force reader-loop exit, assert the callback fires with a failure status.

### [P2] D2D device-loss recovery recreates only the swap chain, never the D2D device/context — src/ui/src/Renderer.cpp:197-204
- Symptom (user-visible): after a GPU driver reset/TDR or adapter change the workspace can stay blank/frozen for the rest of the session (app must be restarted), despite the "device loss recovered by Renderer" claim.
- Evidence: `EndFrame` treats a failed `EndDraw` (which with the device-context API returns `D2DERR_RECREATE_TARGET` when the **D2D device** is lost) by calling `RecreateSwapChainAndTarget()` (`Renderer.cpp:197-199`), which only recreates the DXGI swap chain + target bitmap (`Renderer.cpp:126-159`) and never `d2dDevice_`/`d2dContext_`. A context whose device was removed keeps failing `BeginDraw`/bitmap creation; `ConsumeDeviceRecreated()` then fans out brush resets (`WindowShell.cpp:1285`, `MarkDeviceDirty` at 1134) onto the same broken context.
- Failure scenario: driver reset mid-frame → `EndDraw` fails → swap chain recreated against the dead `d3dDevice_`/`d2dContext_` → every subsequent frame fails identically; `RecreateSwapChainAndTarget`'s failure returns are ignored (`Renderer.cpp:199`), so nothing retries.
- Root cause hypothesis: recovery path predates/excludes full device re-creation; only target-level loss is handled.
- Suggested fix: on `D2DERR_RECREATE_TARGET` (or repeated `RecreateSwapChainAndTarget` failure), rebuild the chain d3dDevice→dxgi→d2dFactory device→context→target and set `deviceRecreated_`; count consecutive failed recoveries and surface a diagnostic instead of looping silently.
- Regression test idea: hard to unit-test; at minimum add a debug hook forcing `EndDraw` failure N times and assert the renderer ends with a usable context (injectable factory seam).

### [P3] IconCache worker thread uses shell icon APIs without COM initialization — src/ui/src/IconCache.cpp:49-80
- Symptom (user-visible): intermittently missing/generic icons for types whose icons come from shell icon handlers (COM `IExtractIcon` implementations reached via `SHGetFileInfoW`).
- Evidence: `WorkerMain` (`IconCache.cpp:49`) calls `ResolveIcon` → `SHGetFileInfoW`/`SHGetStockIconInfo` (`IconCache.cpp:90,117`) on a raw `std::thread` with no `CoInitializeEx` anywhere in the file; contrast Preview's workers which do pair `CoInitializeEx` per item (`Preview.cpp:250-254`).
- Failure scenario: shell resolves a ProgID whose icon handler is an out-of-proc/COM-based extractor → `CoCreateInstance` inside shlwapi fails on an uninitialized thread → `SHGetFileInfoW` returns no icon → permanent glyph fallback for that type.
- Suggested fix: `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` (or MTA) at the top of `WorkerMain`, `CoUninitialize` on return.
- Regression test idea: hard to assert deterministically; at least document/assert HRESULT of init in debug.

### [P3] `FileOperations::workerThreadId_` published unsynchronized; enqueue can be stranded — src/ui/src/FileOperations.cpp:212
- Symptom (user-visible): very rarely, a copy/move/delete queued right at startup (or right after `Stop()`+`Start()`) does not begin executing until the *next* operation is enqueued.
- Evidence: worker writes `workerThreadId_ = GetCurrentThreadId()` (`FileOperations.cpp:212`) with no atomic/acquire-release; `Enqueue` reads it (`FileOperations.cpp:199`) and skips `PostThreadMessageW` when it observes 0. The worker's own kickoff post (`:215`) drains the queue as of that instant; an `Enqueue` that read stale 0 concurrently can append after the kickoff drain.
- Suggested fix: make `workerThreadId_` `std::atomic<DWORD>` and publish it before the kickoff post (already ordered), or have `Enqueue` fall back to signaling via a condition variable the worker waits on.
- Regression test idea: stress test enqueueing from immediately after `Start()` in a loop; assert every request produces exactly one `Completed`/`Cancelled` event.

## Verified sound
- **EngineClient shutdown/cancellation**: overlapped I/O cancelled by exact handle before joins; `Stop()` vs slow-handshake race guarded by double-checked `running_`/`pipe_` under both locks (`EngineClient.cpp:340-352`); retired-pipe list prevents use-after-close between writer failure and `ClosePipe` (`EngineClient.cpp:848-876`); invalidation queue deliberately retains requests until the frame is accepted, with reconnect wake-up (`EngineClient.cpp:772-795`).
- **Snapshot publication**: generation field written last as commit marker after payload serialization (`SnapshotPublisher.cpp:170-190`); publisher-side unmap/close of the previous section is safe because the UI holds its own mapping reference; UI validates format/generation/payload bounds against the live mapping before parsing (`EngineClient.cpp:47-68,239-266`); read-only mapping load rationale documented and correct (`EngineClient.cpp:31-40`).
- **DirectoryWatcher**: stop-event + `CancelIoEx` before join; startup handshake prevents racing the first change; zero-byte completion treated as overflow; bounded overflow give-up; terminal-error final refresh; eviction LRU kept consistent (`DirectoryWatcher.cpp:39-140,243-300,302-319`).
- **UI marshaling discipline**: all EngineClient callbacks either touch atomics/mutex-guarded ColumnView state (`ColumnView.h:418-428`) or marshal via `PostMessageW` with heap payloads (`WindowShell.cpp:1026-1048,1423-1435`, `Util.cpp:26`); TreemapView additionally guards with a shared lifetime token (`TreemapView.cpp:225,241`).
- **PreviewController**: cancellation token + request-identity double-check before invoking completion; destructor sets `stopping_` under the same lock the worker re-checks, so no post-destruction callback (`Preview.cpp:203-232,259-271`).
- **OLE/COM apartments**: UI thread `OleInitialize` paired (`Main.cpp:39-55`); drag-drop source/target refcounting correct; `RevokeDragDrop` + `dropTarget_.Reset()` in WM_DESTROY before hwnd teardown (`WindowShell.cpp:2281-2296`); FileOperations worker uses STA with proper pairing and Stop() drains pending conflict dialogs and answers them so the worker can't wait forever (`FileOperations.cpp:169-190`).
- **UiServer stop ordering**: connection gate ensures no `HandleConnection` callback outlives `Stop()`; clients cancelled before watcher join; listener stopped before gate drain (`UiServer.cpp:312-352`).
- **VolumeSessionManager**: single reconciliation thread joined before connection stop; callbacks registered to the connection are torn down with it (`VolumeSessionManager.cpp:224-249`).
