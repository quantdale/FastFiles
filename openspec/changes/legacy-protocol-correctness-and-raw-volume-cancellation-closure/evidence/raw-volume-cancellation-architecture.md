# Raw-volume cancellation architecture evaluation (task 4.1–4.4)

Change: `legacy-protocol-correctness-and-raw-volume-cancellation-closure`
Fixture: `tests/indexsvc/test_volume_scanner_terminal.cpp` (`ffvolumescanner_terminal_tests`,
also part of the hosted ASan slice and the local CI-parity asan step).

Invariant under test: **a cancellation request is not completed I/O.**

## 1. Where the synchronous raw-volume I/O lives (4.1)

| # | file:line | FSCTL | overlapped | owning thread | resource lifetime |
|---|-----------|-------|-----------|---------------|-------------------|
| 1 | `src/indexsvc/src/VolumeScanner.cpp` (volume data) | `FSCTL_GET_NTFS_VOLUME_DATA` | no (`lpOverlapped == nullptr`) | scan worker | `ScopedVolumeHandle`, closed by RAII at scan end |
| 2 | `src/indexsvc/src/VolumeScanner.cpp` (enumeration loop) | `FSCTL_ENUM_USN_DATA` | no | scan worker | same handle |
| 3 | `src/indexsvc/src/VolumeScanner.cpp` (per record) | `FSCTL_GET_NTFS_FILE_RECORD` | no | scan worker | same handle |
| 4 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_GET_NTFS_FILE_RECORD` | no | journal worker | raw `HANDLE`, `CloseHandle` at stream end |
| 5 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_QUERY_USN_JOURNAL` | no | journal worker | same handle |
| 6 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_CREATE_USN_JOURNAL` | no | journal worker | same handle |
| 7 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_QUERY_USN_JOURNAL` (retry) | no | journal worker | same handle |
| 8 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_GET_NTFS_VOLUME_DATA` | no | journal worker | same handle |
| 9 | `src/indexsvc/src/UsnJournalReader.cpp` | `FSCTL_READ_USN_JOURNAL` | no | journal worker | same handle |

Volume handles are opened `FILE_FLAG_BACKUP_SEMANTICS` **without**
`FILE_FLAG_OVERLAPPED`, and no `OVERLAPPED` structure or `GetOverlappedResult`
appears anywhere in `src/indexsvc`. `CancelIoEx` is used only on the Ctrl pipe
(`ScanWorkerBoundary.h`, `ServiceConnection.cpp`); `CancelSynchronousIo` is used
only on the worker thread (`ServiceWorkerLifecycle.h`).

Teardown entry points that can wait on a worker: `StopVolumeScan`, supersede in
`StartVolumeScan`/`OpenUsnJournal`, disconnect (`StopAndJoinAll`), and service
shutdown (`PipeListener::Stop` → `JoinConnectionThreads` → each connection's
`StopAndJoinAll`). Every one of them ends in an unbounded `std::thread::join()`.

## 2. Hermetic stall fixture and separate measurements (4.2/4.3)

The fixture parks a worker in `WaitForSingleObject` on an event the test
releases, which has exactly the observable shape of a synchronous kernel call
that has not returned, and which `CancelSynchronousIo` cannot cancel (it returns
`ERROR_NOT_FOUND`; `RequestWorkerStop` treats that as "no synchronous request in
flight", which is the documented request-only semantics). No production volume
is opened or stalled. The scan-worker cases run the real production scan
decision where practical; the journal case exercises the same production
`ActiveWorker`/`RequestWorkerStop`/`StopAndJoin(All)` lifecycle with a
journal-shaped worker (the raw `FSCTL_READ_USN_JOURNAL` call itself is
**UNPROVEN** here — see §3(a)).

| Guarantee | Evidence class | Recorded result |
|---|---|---|
| Time until the stop flag is published / a cancellation is requested | **measured** | immediate; the flag is published before any wait. In this fixture the thread is parked in a non-I/O wait, so `CancelSynchronousIo` reports `ERROR_NOT_FOUND` ("no synchronous request in flight") — no I/O was cancelled in this run. The real cancel of in-flight pipe I/O is measured separately by `TestStopAndJoinCancelsSynchronousWorkerIo` (`ERROR_OPERATION_ABORTED`). |
| Time until the control path responds | **measured** | the join is still parked 400 ms after the request; the architecture does not bound this |
| Time until the worker stops | **measured** | 15 ms after the parked operation finally returns (single recorded run; the asserted bound is < 3 s) |
| Whether cleanup requires kernel I/O completion | **measured** | yes — `StopAndJoin(All)` returns only once the operation completes |
| Whether new work can be admitted | **derived from code order, not measured** | `ServiceConnection.cpp` supersede/admission is serialized behind `StopAndJoin` (`StartVolumeScan` joins the previous worker before the replacement slot is created), so no admission happens while parked. The fixture only restates that ordering. |
| Whether the service can shut down | **derived from code order, not measured** | `PipeListener::Stop` → `JoinConnectionThreads` → `StopAndJoinAll` has the same parked property; no service-level fixture stands a listener up while a worker is parked, so the shutdown bound is **UNPROVEN**. |
| Whether a previous worker can affect a replacement session | **derived structurally, not measured** | a worker holds its generation's pipe `HANDLE` value and the connection's `writeMutex`; both stay valid until `join()` returns, and the kernel cannot recycle the handle before it is closed, so a late worker can only write to its own dead instance. `test_volume_scanner_terminal.cpp` proves the write-suppression half (a worker whose connection-abort flag is set gets `TransportFailed` and writes nothing); no fixture models a full replacement connection, so the replacement-session claim is **not measured**. |
| Cancellation wins over a pre-enumeration setup failure | **measured** | `TestStopDuringSetupFailureStaysSilent` for `{2,2}`, `{2,1}` and `{1,5}`: a stop published during a failing setup call returns `Cancelled`, requests no abort, and writes no terminal |
| A late-completing scan operation emits nothing after a stop | **measured** | `TestLateCompletingScanOperationEmitsNothingAfterStop`: the real `RunSyntheticVolumeScanForTesting` path returns `Cancelled` after the parked source returns, with no batch and no terminal |
| A late worker on a torn-down connection writes no batch/terminal | **measured** | `TestDeadConnectionSuppressesLateWorkerWrites`: the production `transportFailed` flag turns the frame write into `TransportFailed`; zero bytes reach the pipe |
| Journal worker stop uses the same bounded request | **measured (lifecycle)** | `TestJournalWorkerStopUsesTheSameBoundedRequest`: request returns immediately; the owner waits for the parked operation; per-volume stop leaves the other journal untouched; a late journal frame write is suppressed by the same production writer |
| Repeated stalled stop/start cycles do not accumulate | **measured** | three cycles, each drained, each < 2 s |
| Supersede/admission ordering with a parked worker | **measured** | `TestSupersedeDoesNotAdmitReplacementsWhileStalled`: the owner thread exclusively owns the worker vector; the replacement slot appears only after the parked worker returns |

## 3. Architecture evaluation (4.4)

### (a) Overlapped I/O + `CancelIoEx`

Not selected. Validating it requires opening a real raw volume handle — which
needs elevation (`SeBackupPrivilege`) — for every FSCTL and handle shape, on
each supported Windows version. That is exactly the elevated live-volume
certification boundary recorded in `verify/certification/external-frontier.json`
and is unavailable here. Per-FSCTL overlapped support (for the scanner and the
journal `FSCTL_READ_USN_JOURNAL` path alike) therefore remains **UNPROVEN**;
implementing it now would be guesswork about security- and lifecycle-relevant
kernel behavior, which the campaign forbids.

### (b) Dedicated worker ownership + explicit lifecycle + bounded admission

Evaluated and **deferred**, not landed. The current ownership model is
per-connection `ActiveWorker { volumeId, stopFlag, thread }` with the worker
lambda capturing the connection's pipe handle by value and the connection's
`writeMutex`/`transportFailed` by reference. Bounding the control path (so
`Stop`/disconnect/shutdown return while a worker remains in kernel) requires
abandoning workers safely, which in turn requires:

1. a heap-owned, reference-counted per-connection I/O context (pipe handle +
   write mutex + connection-abort flag) so no worker ever references a
   connection's stack frame;
2. a non-close way to end the connection for the peer (`DisconnectNamedPipe`)
   while the context keeps the handle valid for a worker still inside a write;
3. a process-wide, capped quarantine for workers that outlive the stop grace,
   with fail-closed admission once the cap is reached, plus detach/reap
   bookkeeping to avoid unbounded accumulation;
4. a replacement-session guard so a late worker can never write into, or
   complete a scan for, a later connection.

That is a substantial privileged-service lifecycle redesign. Its new
guarantees can be *stated* and unit-tested, but the campaign's rule is to
implement a production-safe architecture "only after 4.4 records its supported
guarantees", and the decisive guarantee (that the quarantine is only entered
for genuine non-cooperative kernel completions, and that the forced connection
end is safe for in-flight writes) can only be exercised against a real raw
volume and driver — the same external boundary as (a). Landing it now, with
hosted checks billing-blocked, would be a speculative change to teardown
ordering of the privileged service. It is recorded as `FUTURE_PRODUCT_WORK`
with its unlock conditions below.

### (c) Isolated helper process

Not selected. It is the design's last resort ("only if in-process containment
cannot stop a stuck worker from blocking shutdown or writing to a replacement
connection"). A helper process would add a new privileged surface for no
truthfulness gain: the helper's raw-volume worker is subject to the same kernel
completion behavior, and killing the helper would reintroduce
`TerminateThread`-equivalent risk. It stays `FUTURE_PRODUCT_WORK`, gated on the
in-process options being proven insufficient against a real driver.

### (d) Keep request + join, report only what is proven (selected)

The narrowest production-safe architecture for this campaign, and the one whose
guarantees the fixture actually establishes. It adds no new privileged surface,
keeps the closed command surface, authentication, connection-scoped identities,
and fail-closed behavior untouched, and never reports a request as completion.
What it explicitly does **not** provide is a finite completion bound or a
bounded control-plane return while a worker is parked; those stay `UNPROVEN`.

Blast radius (reviewer finding, report-only): because a scan failure that no
negotiated peer can be told about ends the whole connection, one legacy
volume's failing scan also interrupts every other volume's scan/journal worker
on that connection and forces a reconnect plus re-enumeration. That is the
design's chosen trade-off (D3: prefer teardown over a false-success terminal);
durable cursors and `ResumeUsn` make the interrupted journal streams resume
without data loss, and the engine's new per-volume reissue floor keeps the
retry cadence bounded.

### Rejected outright

`TerminateThread`; detaching a worker that still touches stack-owned state;
closing the pipe/volume handle from another thread while an operation may still
reference it; unbounded detached-worker accumulation; reporting a requested
cancellation as completed I/O or claiming a universal handler deadline against
a non-cooperative storage driver.

## 4. What that means for the ledger

* Inherited raw-volume completion bound (predecessor task 2.3) and this
  campaign's 5.4 stay **unchecked**: no finite completion bound is proven for
  an arbitrary synchronous raw-volume `DeviceIoControl`.
* Named-pipe cancellation remains a separate, already-proven guarantee and is
  never reported as a raw-volume result.
* The unreportable-legacy-failure abort path has an explicit recovery bound:
  the engine records a per-durable-volume reissue floor (five seconds, the same
  cadence a delivered `ScanFailed` uses) and the existing bounded retry
  scheduler reissues `StartVolumeScan` when it expires
  (`VolumeSessionManager::ScanReissueFloorLocked` / `ArmScanReissue`), including
  for an aborted reconciliation.
* Unlock for the deferred architecture: an elevated Windows host with a raw
  NTFS volume where each FSCTL's overlapped/`CancelIoEx` behavior can be
  measured, plus hosted checks restored — then implement (a) for the FSCTLs
  that actually support it, and (b)/(c) only for those that do not.
* Pre-existing, not introduced by this change and not demonstrated reachable
  here: the journal worker may write `UsnJournalOpened`/`JournalResumeInvalid`
  after a stop was requested (`UsnJournalReader.cpp`, `ServiceConnection.cpp`)
  — no false success, but a pre-existing post-stop frame; and a malformed
  trailing enumeration record can be skipped while the cursor advances
  (`VolumeScanner.cpp`) — Windows builds those buffers, so reachability is
  **UNVERIFIED**. Both are recorded as residuals rather than changed
  speculatively.
