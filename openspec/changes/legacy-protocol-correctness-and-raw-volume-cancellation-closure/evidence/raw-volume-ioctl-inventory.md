# Stream B — Raw-volume DeviceIoControl cancellation inventory

Repository: `D:/Documents/tryPython/FastFiles` (read-only investigation). Change under apply:
`openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure` (stream B ⇒ task 4.x/5.x).

Scope: every raw-volume `DeviceIoControl` in the scanner (`VolumeScanner.cpp`) and journal
(`UsnJournalReader.cpp`) paths, plus the worker/teardown machinery that cancels and joins them.

## DeviceIoControl site table (one row per call site)

All sites use `lpOverlapped == nullptr` ⇒ **synchronous** calls on **non-overlapped** volume handles
(volume open flags are `FILE_FLAG_BACKUP_SEMANTICS` only, no `FILE_FLAG_OVERLAPPED`; see below).
Handler column = the thread that issued the IOCTL and owns the handle for the call's lifetime.

| # | file:line | FSCTL | overlapped | owning thread | handle / resource lifetime |
|---|-----------|-------|-----------|---------------|-----------------------------|
| 1 | `src/indexsvc/src/VolumeScanner.cpp:326` | `FSCTL_GET_NTFS_VOLUME_DATA` | no (nullptr) | scan worker thread (`ServiceConnection.cpp:317`) | `ScopedVolumeHandle` created `VolumeScanner.cpp:309`, `reset()` on failure at 329, final `volumeHandle.reset()` at 462 |
| 2 | `src/indexsvc/src/VolumeScanner.cpp:373` | `FSCTL_ENUM_USN_DATA` | no (nullptr) | scan worker thread | same `ScopedVolumeHandle` `volumeHandle.get()`; closed via RAII reset at 462 / dtor |
| 3 | `src/indexsvc/src/VolumeScanner.cpp:416` | `FSCTL_GET_NTFS_FILE_RECORD` | no (nullptr) | scan worker thread | same `ScopedVolumeHandle`; one call per enumerated record |
| 4 | `src/indexsvc/src/UsnJournalReader.cpp:139` | `FSCTL_GET_NTFS_FILE_RECORD` | no (nullptr) | journal worker thread (`ServiceConnection.cpp:400`) | raw `HANDLE volumeHandle` created `UsnJournalReader.cpp:215`, closed `CloseHandle` at 372 |
| 5 | `src/indexsvc/src/UsnJournalReader.cpp:171` | `FSCTL_QUERY_USN_JOURNAL` | no (nullptr) | journal worker thread | same raw handle (via `QueryOrCreateUsnJournal`) |
| 6 | `src/indexsvc/src/UsnJournalReader.cpp:178` | `FSCTL_CREATE_USN_JOURNAL` | no (nullptr) | journal worker thread | same raw handle (create-if-not-active path) |
| 7 | `src/indexsvc/src/UsnJournalReader.cpp:180` | `FSCTL_QUERY_USN_JOURNAL` (retry) | no (nullptr) | journal worker thread | same raw handle |
| 8 | `src/indexsvc/src/UsnJournalReader.cpp:224` | `FSCTL_GET_NTFS_VOLUME_DATA` | no (nullptr) | journal worker thread | same raw handle |
| 9 | `src/indexsvc/src/UsnJournalReader.cpp:255` | `FSCTL_READ_USN_JOURNAL` | no (nullptr) | journal worker thread | same raw handle; steady-state loop IOCTL |

Out of scope but present in the same target (startup diagnostic probes, not scanner/journal stream):
`PrivilegeVerification.cpp:264,286,360` (`FSCTL_QUERY_USN_JOURNAL`, `FSCTL_READ_USN_JOURNAL`) — run under
`VerifyBackupPrivilegeSufficiency()` at `Main.cpp:246`, synchronous, own raw handle opened at
`PrivilegeVerification.cpp:254,348`.

### Overlapped / cancellation search (item 5)
`grep -rn "OVERLAPPED|CancelIoEx|GetOverlappedResult|FILE_FLAG_OVERLAPPED|CancelSynchronousIo" src/indexsvc`:
- **No** `FILE_FLAG_OVERLAPPED` or `OVERLAPPED` structure on any volume handle. Raw volumes are opened
  `FILE_FLAG_BACKUP_SEMANTICS` (`VolumeScanner.cpp:310`, `UsnJournalReader.cpp:216`).
- `CancelIoEx` appears only at `ScanWorkerBoundary.h:30,39,45` and `ServiceConnection.cpp:412` — all on the
  **pipe** handle (`pipeHandle`), never on a volume handle.
- `CancelSynchronousIo` appears only at `ServiceWorkerLifecycle.h:29`, on the **worker thread**.
- No `GetOverlappedResult` anywhere in `src/indexsvc`.
- The Ctrl pipe itself is overlapped: `ServiceConnection.cpp:56` ("opens Ctrl-pipe instances with
  FILE_FLAG_OVERLAPPED"); framed reads/writes use `ffipc::ReadFrameOverlappedDetailed` / `WriteFrameOverlapped`
  with a `kWriteDeadline{5000}` (`WriteDeadline.h:13`).

## Teardown entry points that can block on join() (no timeout)

All use `std::thread::join()` with **no deadline** (`ServiceWorkerLifecycle.h:41,55`; `PipeListener.cpp:222,241,267`).

1. **`StopAndJoin(activeScans, volumeId)`** — `ServiceConnection.cpp:303` (supersede on `StartVolumeScan`) and
   `:361` (`StopVolumeScan`). Requests stop via `RequestWorkerStop` then `thread.join()`.
2. **`StopAndJoin(activeJournals, volumeId)`** — `ServiceConnection.cpp:388` (`OpenUsnJournal` supersede) and
   `:457` (`CloseUsnJournal`).
3. **`StopAndJoinAll(activeScans) / StopAndJoinAll(activeJournals)`** — `ServiceConnection.cpp:516-517`,
   unconditional disconnect teardown of `RunCtrlConnection`.
4. **`PipeListener::Stop()`** — `Main.cpp:177` (`StopEverything`, invoked from `ServiceCtrlHandler` on
   `SERVICE_CONTROL_STOP`). Signals `shutdownEvent_`, `CancelIoEx` on the pending instance and each
   connection handle, then `JoinConnectionThreads()` (`PipeListener.cpp:284`), which joins every
   connection-callback thread — each of which runs `RunCtrlConnection` and therefore its own
   `StopAndJoinAll` (#3). **Service shutdown transitively blocks on both worker joins.**
5. **`~PipeListener`** (`PipeListener.cpp:25`) calls `Stop()` → same join chain.
6. Indirect: `RunCtrlConnection` returns ⇒ its callback thread exits ⇒ joined by `PipeListener`
   (`JoinConnectionThreads` / `ReapFinishedConnectionThreads` at `PipeListener.cpp:213-223`).

## What `RequestWorkerStop` / `CancelSynchronousIo` does and does not guarantee (item 4)

`ServiceWorkerLifecycle.h:22-33`: sets `stopFlag`, then if the thread is joinable calls
`CancelSynchronousIo(worker.thread.native_handle())`. Comment at `:18-19` states it plainly:
> "CancelSynchronousIo only requests cancellation; it does not provide a completion deadline, so joining
> still waits for the kernel request to return."

Guarantees: requests cancellation of the worker thread's currently-parked synchronous I/O (and returns
`ERROR_NOT_FOUND` when none is in flight, treated as success). Does **not** guarantee the IOCTL returns —
`StopAndJoin`/`StopAndJoinAll` then call an **unbounded `thread.join()`**. The scanner's own loop only
re-checks `shouldStop` *between* `DeviceIoControl` calls (`VolumeScanner.cpp:370` loop condition; the
per-record loop at :382), so an in-flight enum/fetch/driver-stalled IOCTL must complete at the kernel before
the worker can observe the flag. Journal worker likewise (`UsnJournalReader.cpp:243` loop). The idle poll is
sliced (1 s total in 100 ms steps, `UsnJournalReader.cpp:24-25,368-369`) so an *idle* journal thread joins
quickly, but this does not bound an in-flight `FSCTL_READ_USN_JOURNAL`/`FSCTL_GET_NTFS_FILE_RECORD` stall.

## Supersede / admission and stale-worker reach (item 6)

- `StartVolumeScan`: `registry.TryMarkVolumeScanStarted` first (`ServiceConnection.cpp:290`), then
  `StopAndJoin(activeScans, request.volumeId.value)` at **:303** *before* the new `ActiveWorker` is pushed
  (`:316`) and its thread started (`:317`). So supersede is **synchronous and ordered**: the previous worker
  is stopped+joined before the replacement exists. If the previous worker is stuck, this call blocks on
  `join()` and **no replacement is admitted** — the control loop cannot process further requests (incl.
  Heartbeat/Stop) while blocked.
- `OpenUsnJournal` mirrors this: `TryMarkUsnJournalOpened` (:379) → `StopAndJoin(activeJournals,…)` (:388) →
  push/start (:397/:400).
- `activeScans`/`activeJournals` are **per-connection locals** (`ServiceConnection.cpp:189-190`), so a worker
  can only be superseded/joined by its own `RunCtrlConnection` generation. Cross-connection supersede is
  rejected before any worker work: `TryMarkVolumeScanStarted` false ⇒ `disconnect` (`:290-297`);
  `TryStopVolumeScan` from another connection rejected (`:352`).
- Stale worker writing to a later connection: **UNPROVEN / not found**. Each worker captures its own
  `pipeHandle` and `&writeMutex` by value/reference in the per-connection frame (`:317-345`,`:400-441`); a
  later connection gets a new `pipeHandle`/`writeMutex`. `StopAndJoinAll` at disconnect (`:516-517`) joins
  before `CloseHandle(pipeHandle)` (`:519`), so within a generation no worker outlives its pipe. A worker
  stuck in an unbounded IOCTL would keep that *generation's* `writeMutex`/handle alive until the kernel call
  returns; whether it can mutate a *replacement* connection's state depends on the stuck join blocking the
  old generation's teardown — this interaction is not proven by inspection here (no test observed to cover
  an indefinitely-stuck IOCTL).

## Proven vs unproven

**Proven by inspection (file:line cited):**
- All 9 scanner/journal FSCTLs are issued synchronously (`lpOverlapped == nullptr`) on non-overlapped
  `FILE_FLAG_BACKUP_SEMANTICS` volume handles — no `OVERLAPPED`, no `GetOverlappedResult` in the tree.
- `CancelIoEx`/`CancelSynchronousIo` never target a volume handle; `CancelSynchronousIo` targets the worker
  thread only; `CancelIoEx` targets the Ctrl pipe handle only.
- Every teardown entry point (Stop/Close/supersede/disconnect/service shutdown) ends in an **unbounded
  `thread.join()`**; there is no join deadline anywhere.
- `CancelSynchronousIo` is documented-and-coded as a *request only*; a bounded time for an in-flight
  synchronous raw-volume IOCTL to return after cancellation is **not** provided.
- Supersede on `StartVolumeScan`/`OpenUsnJournal` is ordered (stop+join before replacement admitted); a
  stuck prior worker blocks admission of the replacement and (via #4) service shutdown.

**Unproven / open (do not claim complete):**
- Whether an in-flight `FSCTL_*` can be made to return promptly by `CancelSynchronousIo` for arbitrary
  volumes/drivers — no finite bound proven (matches tasks `5.4`, "leave unchecked").
- Whether a stuck worker can write to a later connection's state — no test/evidence found.
- Runtime behavior of cancellation during an active scan/journal — no fixture observed (task 4.2 pending).
