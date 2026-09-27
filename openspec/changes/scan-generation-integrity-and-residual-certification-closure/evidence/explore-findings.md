# Explore: scan-generation integrity and residual certification

Read-only trace for the campaign in `D:\Downloads\fastfiles.txt`. No product code was run or edited. Suspicions are not promoted to executed failures.

This change does not replace `legacy-protocol-correctness-and-raw-volume-cancellation-closure`. At the **pre-registration exploration snapshot**, that change was the sole `active` entry in `openspec/lifecycle.json`; `openspec list --json` then showed it `in-progress` at 37/38 tasks. Apply later registered this change as a second active entry. This exploration record is retained as a historical snapshot. The predecessor's task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 remain `[ ]`.

## Live repository

Queried this session after `git fetch origin`. The prompt's remembered SHA is recorded because `git rev-parse` returned it, not because the prompt was trusted.

| Fact | Live value |
| --- | --- |
| Branch | `main` |
| HEAD | `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` |
| `origin/main` | same SHA |
| Ahead / behind | 0 / 0 |
| `git status -sb` | `## main...origin/main` |
| `git status --porcelain=v1 -uall` | empty |
| Remote | `origin` `https://github.com/quantdale/FastFiles.git` |
| Other worktree | `.kilo/worktrees/scythe-cloud` detached at `0dc3356` |
| Recent tip | `1172c1a` `docs(openspec): record the legacy-protocol campaign A-J handoff` |

`gh pr view 9` returned `state: MERGED`, `mergedAt: 2026-09-27T07:14:33Z`, `mergeCommit.oid` equal to the HEAD above, title "Legacy scan-failure truthfulness and raw-volume cancellation closure".

## Finding A — pause/resume scan authority

### Confirmed in source

`VolumeSession` keeps one `scanBatchFailed` flag and one `scanInProgress` flag per connection-scoped session (`src/engine/src/VolumeSessionManager.h`). Nothing in that struct names a scan attempt.

`ApplyScanBatch` (`VolumeSessionManager.cpp`) drops a batch while `IsPausedLocked` is true, sets `scanBatchFailed`, and does not advance the cursor. A failed `apply` does the same. Pause does not send `StopVolumeScan`. The service scan keeps running.

`SetIndexingPaused(false)` calls `ResumeAffectedSessions`, which calls `StartOrResumeVolume` for each affected session. When volume metadata is missing or `scanComplete` is false, and `ScanReissueFloorLocked` has no unexpired floor, `StartOrResumeVolume` sets `scanInProgress = true`, `scanBatchFailed = false`, and `reconciliationScan = false`, then sends `StartVolumeScan` for the same `VolumeId`.

`OnScanComplete` returns without authority when the session is missing or `scanInProgress` is false. When the session is in progress, not paused, and `scanBatchFailed` is false, it calls `IndexPipeline::MarkScanComplete`. `ScanCompletePayload` and `ScanFailedPayload` (`src/protocol/include/ffprotocol/Commands.h`) carry only `VolumeId`. The engine dispatch (`PrivilegedConnection.cpp`) passes that id through with an exact-length check and no attempt id.

`SetIndexingPaused` is reached from the UI control pipe (`src/engine/src/Main.cpp` assigns `uiServer.onSetIndexingPaused`). `ScanComplete` is dispatched on the privileged connection's reader. Those are different threads, and the flag clear and the terminal read are separate critical sections.

On the service, `StartVolumeScan` (`src/indexsvc/src/ServiceConnection.cpp`) calls `StopAndJoin` before starting the replacement worker. `ScanOutput::Finish` (`VolumeScanner.cpp`) returns `Cancelled` without a terminal when `shouldStop` is already set. `RequestWorkerStop` (`ServiceWorkerLifecycle.h`) sets that flag and calls `CancelSynchronousIo`; the join still waits for the call to return. A terminal already written to the pipe is not recalled. `StopAndJoin` does not by itself tell the engine which later `ScanComplete` belonged to which attempt.

Call order the functions implement, not a test that was executed: incomplete initial scan, batch applied, pause, batch dropped (`scanBatchFailed` true), then `SetIndexingPaused(false)`, then `OnScanComplete` for the same `VolumeId`. Resume clears the flag while leaving `scanInProgress` true, so `OnScanComplete` takes the `MarkScanComplete` branch. A later `OnScanFailed` or `OnScanComplete` sees `scanInProgress == false` and returns at the "without scan in progress" guard, so it does not undo that mark and does not arm `failedScanRetries_`.

### What the existing tests actually cover

`TestPausedReconciliationDropIsNonAuthoritative` (`tests/engine/test_volume_session_manager.cpp`) marks the volume complete before the pause, so resume does not enter the `!scanComplete` block that clears `scanBatchFailed`. The following terminal still sees the flag.

`TestPausedCompletionIsNotAuthoritative` delivers `ScanComplete` while the pause is still on, so `OnScanComplete` rejects it for `paused`.

`TestBatchFailedScanCompletionIsNotAuthoritative` delivers the failed-batch terminal before resume. Resume then clears the flag for a later clean scan. It does not deliver the original terminal after that clear.

None of those tests is the initial-scan interleaving above. A passing paused-reconciliation test does not close Finding A.

### Unreproduced

This session did not execute the callback order, did not build a pipe fixture, and did not watch a production reader race a UI resume. Whether a real pipe delivers an already-written terminal after resume, or whether `StopAndJoin` always wins before `Finish` in the service-mediated path, is not measured here. The source makes the dangerous callback order authoritative. That is a confirmed control-flow fact, not a captured failing test.

A local generation counter incremented when the replacement starts does not label a `ScanComplete` that still carries only `VolumeId`. Old and new terminals stay indistinguishable on the wire until some other ordering or retirement rule decides which one is current.

```
paused drop                         resume                         old terminal
scanBatchFailed = true              StartOrResumeVolume            OnScanComplete
cursor frozen                       scanBatchFailed = false        paused == false
scanInProgress stays true           scanInProgress stays true      batchFailed == false
                                    StartVolumeScan (same id)      MarkScanComplete
```

Candidates, none selected: retire the in-flight attempt before reissue and ignore terminals that cannot be ordered after that retirement; wait until the old worker is known finished before clearing authority; tear down the connection so the old `VolumeId` dies with it; add an additive terminal discriminator only if in-process ordering cannot be made reliable. `openspec/changes/establish-architecture-foundation/design.md` D4 is the closed command surface. No option may add a generic open-path or open-handle command.

## Finding B — interrupted reconciliation retry

### Confirmed in source

`kScanFailureRetryDelay` is 5 seconds. `kSchedulerPollInterval` is 10 minutes. `kReconciliationIntervalFileTime` is 6 hours (`VolumeSessionManager.cpp`).

On a non-Active connection, `OnConnectionStateChanged` copies in-flight scans into `scanReissueFloors_` keyed by durable volume id, with due `now + 5s` and the session's `reconciliationScan` bit. It then clears `sessionsByEphemeralId_` and `failedScanRetries_`, and aborts an active reconciliation pass so a partial seen-set cannot delete rows. That abort is the right non-authority behavior. It also drops the only queue the scheduler knows how to wake on.

`ScanReissueFloorLocked` returns the due time and erases an expired floor. It never reads the stored reconciliation bit.

`StartOrResumeVolume` consults the floor only when the volume is not yet `scanComplete`. An already-indexed volume skips that block and only reopens the journal. Reconnect therefore does not arm `failedScanRetries_` for the interrupted reconciliation.

`TriggerReconciliation` does consult the floor and would arm a reconciliation retry, but reconnect does not call it for an unchanged journal. Callers are: journal-id mismatch, invalid journal resume, a failed journal-batch apply, a deferred reconciliation after an authoritative completion, the scheduler when the 6-hour interval is due, and the scheduler's retry map. The scheduler's early wake looks at `failedScanRetries_` only. `scanReissueFloors_` does not notify `wakeCv_`.

After the 5-second due time has passed, the next `TriggerReconciliation` treats the floor as expired and starts immediately. The five-second queue was never the wake. The ordinary poll (10 minutes) plus the 6-hour interval is what reaches that call for an already-indexed volume whose journal id did not change.

`TestLegacyFailureReissueIsThrottledAfterConnectionLoss` covers an initial scan that is not `scanComplete`: reconnect goes through `StartOrResumeVolume`, which arms the retry map. It does not start from an already-indexed reconciliation.

Disconnect already refuses a terminal for the cleared ephemeral id (`OnScanComplete` returns when the session is gone). The gap is the missing transfer of the durable floor onto the new ephemeral id's retry slot.

### Unreproduced

No timer trace was captured. The control flow above is the source. It was not executed against a clock in this session.

## Finding C — many-tabs visual count

### Confirmed in source

`verify/capabilities/windows-ui-visual-validation/VisualValidation.psm1` scenario `shell-many-tabs-overflow` uses action `many-tabs`, width 720, and passes only when `Get-VisualTabCount` is at least 8. That helper counts `Button` elements under the main window whose `AutomationId` matches `^830`. The default UIA view is Control (`Resolve-UiaView` falls back to `Control`). The action clicks the new-tab control eight times and does not read `NavigationWorkspace::TabCount`, the overflow button, or a menu.

`kTabBaseId` is 8300 (`NavigationChrome.h`). `RebuildTabs` creates one tab button and one close button per `workspace_->TabCount()` entry. `RepositionTabs` calls `ShowWindow(..., SW_HIDE)` for indexes outside `visibleFirst` / `visibleCount`. `ShowTabList` appends a menu entry for every `TabCount()` index, including hidden ones. `ComputeChromeLayout` (`src/ui/include/ffui/ChromeLayout.h`) sets `overflow` and a smaller `visibleCount` when minimum-width tabs do not fit the strip. Hidden is a visibility window, not a removal from `tabs_`.

The predecessor handoff (`openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/evidence/campaign-handoff-report.md`) records `shell-many-tabs-overflow` as count 4 rather than 8, reproduced on the pre-change build. That sentence is inherited evidence. This session did not launch the visual harness.

### Unreproduced

Whether eight logical tabs were created, whether Control view omits `SW_HIDE` buttons, and whether the inherited count of 4 is the visible window, were not measured. A hypothesis that a 720-pixel window at 120 DPI yields a visible capacity of 4 is not a measurement. The expected count must not be lowered from 8 to 4 to make the harness pass, and hidden buttons must not be treated as deleted tabs.

## Finding D — targeted-build identity

### Confirmed in source

`cmake/FastFilesVersion.cmake` defines `fastfiles_build_identity` as an `ALL` custom target that runs `cmake/WriteBuildIdentity.cmake` on every build. `fastfiles_add_version_resource` sets `OBJECT_DEPENDS` on the generated rc-defines file and `add_dependencies` on `fastfiles_build_identity`. Call sites are the UI, engine, service, setup, and `fftest` executables (`src/ui/CMakeLists.txt`, `src/engine/CMakeLists.txt`, `src/indexsvc/CMakeLists.txt`, `src/installer/CMakeLists.txt`, `src/fftest/CMakeLists.txt`).

`WriteBuildIdentity.cmake` rejects an identity that is not 40 hex characters with `FATAL_ERROR`. A malformed `FASTFILES_BUILD_ID` is also `FATAL_ERROR` at configure time. An explicit override that differs from `git rev-parse HEAD` warns and still embeds the override; tree state for an override is `unbound`. Outputs are copied only when bytes change.

The release candidate name set in `verify/release/ReleaseCommon.psm1` is `FastFiles.exe`, `FastFilesEngine.exe`, `FastFilesIndexSvc.exe`, and `FastFilesSetup.exe`.

### Unreproduced

No isolated revision-A then revision-B targeted build was run. Whether those dependencies already refresh every certification-relevant binary, including diagnostic and test binaries that embed identity without the version-resource helper, is unproven. No CMake edit is justified until that sequence exists. A no-code closure is allowed if the sequence shows the current edges are sufficient. Override validation stays fail-closed.

## Finding E — hosted CI and protection

### Live query this session

`gh auth status` is logged in as `quantdale` with `repo` and `workflow` scopes.

`gh run list --repo quantdale/FastFiles --limit 8` returned, as the newest `main` push, run `36302493823` for HEAD `1172c1a1e6aa9c1a726d52abdade0975b6979e4b`, conclusion `failure`, created `2026-09-27T07:14:34Z`. That run id is inherited from the campaign prompt. The following is the live `gh run view` and annotations query of the same id, not a new observation standing in place of that query:

Six jobs, each `steps: []`, durations about 2–4 seconds: `debug`, `release-test`, `analyze`, `asan-protocol-ipc-lifecycle`, `repository-hygiene-and-contracts`, `unsigned-release-candidate-qualification`. Each carries the annotation: "The job was not started because recent account payments have failed or your spending limit needs to be increased. Please check the 'Billing & plans' section in your settings."

`GET /repos/quantdale/FastFiles/branches/main/protection` and `GET /repos/quantdale/FastFiles/rulesets` both returned HTTP 403: "Upgrade to GitHub Pro or make this repository public to enable this feature."

No billing setting, payment method, plan, or repository permission was changed.

### Unreproduced / not claimed

Local CI parity was not run. It would not certify this HEAD on GitHub. The documentation tip `1172c1a` is not, by itself, a certified executable. Older failure runs in the same list (`36261023218` and earlier) are historical context from the same query, not a new green result.

## Raw-volume completion bound

`CancelSynchronousIo` is a request. `ServiceWorkerLifecycle.h` states that joining still waits for the kernel call to return. Legacy-protocol task 5.4 and residual-hardening task 2.3 stay unchecked because no finite completion bound for an arbitrary synchronous raw-volume `DeviceIoControl` was proven. Simulated stall fixtures are not that proof. This campaign does not redesign worker ownership, overlapped FSCTL, `CancelIoEx` policy, or process isolation.

## What the proposal should freeze

Findings A and B need a reproducer or an impossibility proof before any fix, then a combined pause/disconnect/reconnect case before either is called resolved. The correction must reject or retire a stale terminal, keep an incomplete initial scan incomplete, retry missing coverage, refuse to delete valid rows from an incomplete reconciliation, stop a failed replacement from inheriting false authority, keep ordinary pause/resume and successful scans working, and stop a disconnect from restoring stale authority. Interrupted reconciliation of an already-indexed volume must survive a new ephemeral id, honor the five-second floor without a tight loop or the ten-minute poll as the only wake, keep one outstanding retry, and must not send the old connection-scoped id.

Finding C must separate logical tabs, visible tabs, and the harness. Finding D must reproduce targeted-build freshness before CMake edits. Finding E stays an external hold: no merge to `main` while the six hosted jobs are blocked, and no silent treatment of local parity as hosted certification.
