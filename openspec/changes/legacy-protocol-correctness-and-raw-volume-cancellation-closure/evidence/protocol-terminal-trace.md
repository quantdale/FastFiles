# Stream A — Privileged Scan-Terminal Lifecycle Trace

Change under apply: `legacy-protocol-correctness-and-raw-volume-cancellation-closure`. All citations `file:line`.

## 1. Handshake negotiation + compatibility matrix

- `src/protocol/src/Version.cpp:5-12` — `IsVersionCompatible(local,peer)`: true iff `peer.major == local.major` OR `peer.major + 1 == local.major` (one-release overlap). **Minor never gates compatibility.**
- `src/protocol/include/ffprotocol/Version.h:23` — `kCurrentProtocolVersion{2, 2}`.
- `src/indexsvc/src/ServiceConnection.cpp:140` — service rejects (sends `IncompatibleVersion`) if `!IsVersionCompatible(kCurrent, request.clientVersion)`.
- `src/indexsvc/src/ServiceConnection.cpp:156-159` — negotiated `outVersion` = `{kCurrent.major, min(client.minor, kCurrent.minor)}` when majors equal, else the client's own version:
  `outVersion = request.clientVersion.major == kCurrent.major ? {kCurrent.major, min(client.minor,kCurrent.minor)} : request.clientVersion;`
- Engine derives its own negotiated version from the `HandshakeAck` payload (`src/engine/src/PrivilegedConnection.cpp:294-298`), stores `negotiatedMajor_/negotiatedMinor_`; rejects if incompatible or `ack.major > kCurrent.major`.

| Client offers | Compatible? | Negotiated version | Basis |
|---|---|---|---|
| {1,x} | yes (1+1==2) | {1,x} | ServiceConnection.cpp:156 else-branch |
| {2,0} | yes | {2,0} | min(0,2)=0 |
| {2,1} | yes | {2,1} | min(1,2)=1 |
| {2,2} | yes | {2,2} | min(2,2)=2 |
| {3,x} | no | — | 3≠2 and not 3+1==2 → rejected |

## 2. Capability gates

- `Version.h:34-36` `ProtocolSupportsAllocatedSize(v)`: `v.major>2 || (v.major==2 && v.minor>=1)`. True for {2,1},{2,2},{3,x}; **false for {1,x} and {2,0}**.
- `Version.h:42-44` `ProtocolSupportsScanFailure(v)`: `v.major>2 || (v.major==2 && v.minor>=2)`. True only for {2,2}+; **false for {1,x},{2,0},{2,1}**.
- Allocated-size gate consumed at `VolumeScanner.cpp:128` (`SendBatch` → V2 vs downgraded V1 batches).

## 3. `SendScanTerminal` call sites (`VolumeScanner.cpp`)

Definition `VolumeScanner.cpp:237-246`: `if (!success && ProtocolSupportsScanFailure(v)) → ScanFailed ; else → SendScanComplete`. **success=false with a pre-{2,2} version falls through to `SendScanComplete`** (the intentional legacy fallback).

- `success=false` sites (all reachable for every negotiated version): `VolumeScanner.cpp:302` (privilege-enable fail), `:317` (raw-volume open fail), `:329` (FSCTL_GET_NTFS_VOLUME_DATA fail), `:338` (bad geometry). Each returns `FailedTerminalDelivered` if the write succeeded else `TransportFailed`.
- `success=false` mid-enumeration: `ScanOutput::Finish` at `VolumeScanner.cpp:154-165` calls `SendScanTerminal(..., !enumerationFailed, ...)`; `enumerationFailed=true` is set at `:406` (any DeviceIoControl error ≠ `ERROR_HANDLE_EOF`) and `:409-413` (degenerate no-cursor reply). `:158-160` comment: "only a complete enumeration receives ScanComplete (or the capability-gated ScanFailed)".
- `success=true` sites: **only** `ScanOutput::Finish` with `!enumerationFailed` (`:161`), and the synthetic test fixture `RunSyntheticVolumeScanForTesting` (`:254-270`) via the same `Finish`.

## 4. Record delivery + transport failure

- Delivery: `SendScanBatch` (`:55-79`, V1) / `SendScanBatchV2` (`:84-108`); selected in `ScanOutput::SendBatch` (`:127-140`). A failed write sets `writeFailed_`.
- `ScanOutput::Finish` (`:142-165`): if `writeFailed_` → returns `TransportFailed` **without any terminal** (comment `:157-160`). Cancelled (shouldStop) → no terminal.
- `RunScanWorkerBoundary` (`src/indexsvc/src/ScanWorkerBoundary.h:20-47`): `TransportFailed` or any escaped exception sets `transportFailed`, calls `CancelIoEx(pipeHandle,nullptr)`; a partial stream gets **no terminal** in either branch.
- `ServiceConnection.cpp:276` loop condition `while(!disconnect && !transportFailed->load())`; `:323-341` wraps the worker: transport failure/exception emit diagnostics/log ("no terminal appended") and abort the connection. The control loop breaks on `transportFailed` so no further frames are dispatched.

## 5. Engine side

- `PrivilegedConnection.cpp:420-431` `ScanComplete`: exact-length `== sizeof(ScanCompletePayload)` else `protocolViolation=true`; then `scanCompleteCallback_(payload.volumeId)`.
- `PrivilegedConnection.cpp:433-448` `ScanFailed`: exact-length `== sizeof(ScanFailedPayload)` else violation; then `scanFailedCallback_`. Payloads are byte-identical size (`Commands.h:179-192`: both = `VolumeId`).
- Callbacks wired at `VolumeSessionManager.cpp:230-231`.
- Durability during scan: `VolumeSessionManager::ApplyScanBatch` advances cursor per batch via `pipeline_.SetScanCursor(durableId, cursor)` (`:578`) — independent of terminal. On apply failure it sets `scanBatchFailed=true` and freezes cursor (`:556-575`).
- `OnScanComplete` (`:582-676`): clears in-flight state; **if `paused || batchFailed`** → `AbortReconciliationPass` (`:633`), republish, return (no MarkScanComplete). Otherwise `pipeline_.MarkScanComplete(durableId)` (`:654`); if `wasReconciliationScan` and pass active → `FinishReconciliationPass` (`:657`).
- `OnScanFailed` (`:681-733`): clears in-flight state, `AbortReconciliationPass` if active (`:718`), diagnostic `service-reported-scan-failure`, and queues `failedScanRetries_[volumeId] = {now + kScanFailureRetryDelay, wasReconciliationScan}` (`:727-731`). **Never calls MarkScanComplete.** `kScanFailureRetryDelay{5}` at `:58`.
- Retry scheduler `ReconciliationSchedulerLoop` (`:961-1080`): reissues `StartOrResumeVolume`/`TriggerReconciliation` when due (`:1042-1059`); while `!active_` it defers deadlines (`:1001-1006`).
- Reconnect/re-handshake: `OnConnectionStateChanged` (`:285-305`) on any non-Active transition clears `sessionsByEphemeralId_`, `lastSnapshotPublish_`, `failedScanRetries_`; on Active re-issues `EnumerateVolumes` (fresh connection-scoped ids). `StartOrResumeVolume` (`:373-425`) only restarts a scan `if (!meta || !meta->scanComplete)`: once `scanComplete` is true, reconnect does **not** rescan.

## 6. Tests encoding current behavior

- `tests/indexsvc/test_volume_scanner_terminal.cpp:531-546` `main()`: `{2,2}` expects `ScanFailed` (line 535); **`{2,1}` expects `ScanComplete`** on a setup-failed scan (line 541-543), with comment "legacy peers keep today's terminal byte-for-byte". `RunTerminalCase` asserts exactly one terminal frame carrying the `VolumeId` (`:159-170`).
- `tests/engine/test_volume_session_manager.cpp:536-573` `TestScanFailedLeavesPriorIndexAuthorityIntact`: ScanFailed aborts pass, leaves `scanComplete` intact, cadence clock 0, clears in-flight state, duplicate refused, later `scanCompleteCallback_` still marks.
- `tests/engine/test_volume_session_manager.cpp:581-648` `TestScanFailedSchedulesRetry`: 5s cadence, one slot/volume, due retry reissues `StartVolumeScan`.
- `tests/protocol/test_protocol.cpp:343-347`: `ProtocolSupportsScanFailure` truth table ({2,2},{3,0} true; {2,1},{2,0},{1,99} false); `:62-64` equal payload sizes; `:56-59` enum round-trip.
- `tests/indexsvc/test_command_surface.cpp:62-63`: `ScanFailed` rejected as a client request (reply-only).

## Per-version FAILED-scan matrix

Scenario: service (a 2.2 build) negotiated *down* to the client's version, then a scan fails (setup or mid-enumeration). Engine in these rows is this 2.2 build connecting to a peer at the stated negotiated version.

| Ver | (a) Terminal written | (b) Engine handler | (c) MarkScanComplete? | (d) FinishReconciliationPass? | (e) cursor / scanComplete |
|---|---|---|---|---|---|
| 1.x | **`ScanComplete`** (ScanFailed gate false) | `OnScanComplete` | **YES** (`:654`) unless paused/batchFailed | **YES** if `wasReconciliationScan` (`:657`) | cursor already advanced per batch (`:578`); `scanComplete=true` |
| 2.0 | **`ScanComplete`** | `OnScanComplete` | **YES** | **YES** if reconciliation | same as 1.x |
| 2.1 | **`ScanComplete`** (test `:541`) | `OnScanComplete` | **YES** | **YES** if reconciliation | same as 1.x |
| 2.2 | `ScanFailed` (`:240`) | `OnScanFailed` | **NO** | **NO** (Abort at `:718`) | cursor frozen at last applied batch; `scanComplete` unchanged; retry queued |

Note: on a mid-enumeration failure the service stops early (partial coverage) but the engine's `scanBatchFailed` is set only when `ApplyMftBatch` returns false (`:556`), **not** when the service stopped early — so for legacy versions `batchFailed=false` and the terminal is accepted as authoritative.

## Explicit answers

- **(a)** For 1.x/2.0/2.1 the service writes `ScanComplete` even on failure; only 2.2 writes `ScanFailed` (`VolumeScanner.cpp:240-246`).
- **(b)** 1.x/2.0/2.1 → `OnScanComplete` (`VolumeSessionManager.cpp:582`); 2.2 → `OnScanFailed` (`:681`).
- **(c)** `pipeline_.MarkScanComplete` is reached for 1.x/2.0/2.1 failed scans whenever `!paused && !scanBatchFailed` (`:654`); never for 2.2.
- **(d)** `FinishReconciliationPass` can run for legacy failed reconciliation scans (`:657`); 2.2 aborts the pass (`:718`).
- **(e)** Durable cursor advances per applied batch in all versions (`:578`); the **metadata `scanComplete` flag advances to true** for legacy failed scans via `MarkScanComplete`, but not for 2.2.
- **Engine-side negotiated-version check before `MarkScanComplete`: NONE.** `negotiatedMajor_/negotiatedMinor_` are read only by `VolumeSessionManager::BuildStartScanPayload` (`:72` selects V1 vs V2 request) and never consulted in `OnScanComplete`/`OnScanFailed`. `grep negotiatedMajor|ProtocolSupports` in `src/engine/src` shows no gate near line 654.

## Counterexamples / races observed

- Legacy (`{1,x}`/`{2,0}`/`{2,1}`) failed scan → `ScanComplete` → **authoritative completion over incomplete coverage** (`:654`), and a legacy failed *reconciliation* scan finalizes the pass (`:657`) — the exact false-success the change targets. The `{2,1}` test at `test_volume_scanner_terminal.cpp:541-543` currently **encodes this fallback as expected**.
- `scanBatchFailed` does not catch service-side early enumeration stop (only engine-side batch-apply failure), so no defense-in-depth stops the legacy false success.
- Duplicate/stale terminal without an in-flight scan is safely refused (`:600-612`, `:700-710`), verified by `TestDuplicateTerminalsWithoutScanInFlightAreRefused`.
