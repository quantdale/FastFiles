# FastFiles — Post-Certification Adversarial Product Audit — 2026-09-05

Owner-authorized fresh independent product audit campaign. This ledger is the
single audit record. It is NOT an active certification campaign and does not
revoke the repository freeze for any purpose other than reproducing and
repairing genuinely reproduced current-tree product defects.

## Intake (verified live, not copied from prompt)

- START_SHA: `f00eee4dedd48e4bd00672d0284c29391e8a34a7` (== origin/main, 0/0)
- Tree: `6e52d800fc4951695cf65844a3ee2e1d4ca8f171`
- Branch: `main`; worktree: primary only (`C:/Users/Michael Roy/Documents/FastFiles`)
- Preserved unrelated modifications: `.opencode/package.json`, `.opencode/package-lock.json` (untouched)
- Hosted CI on START_SHA: run `33934024026` = SUCCESS, 6/6 jobs (verified via `gh`)
- Open issues / open PRs: none (verified via `gh`)
- `openspec/lifecycle.json`: no `active` change states at intake
- `.agent/EXECUTION_PROMPT.md` / `.agent/NEXT_CAMPAIGN_12H.md`: ARCHIVED — not resumed
- Baseline local verification on START_SHA: Debug build + full CTest **99/99 PASS** (this session)

## Method

Six parallel read-only forensic lanes (file-ops/preview, lifecycle/degraded,
index/USN, UI-state/persistence/routing, security/parsers, blind-spots/smells)
plus central independent source verification of every promoted claim. Line
references below were re-read in the primary worktree, not taken from workers.

## Ledger

States: CANDIDATE / REPRODUCED / INVALIDATED / FIXED / EXTERNAL / ACCEPTED-BEHAVIOR

### REPRODUCED — durable-index false-success cluster (campaign `index-durability-false-success-closure`)

| ID | Sev | Subsystem | Finding | Status |
| -- | --- | --------- | ------- | ------ |
| PA-001 | P1 | service scan + engine reconciliation | Scan setup/enumeration failures are reported as successful completion. `VolumeScanner.cpp:145-189` sends bare `ScanComplete` on privilege-enable failure, raw-open denial, `FSCTL_GET_NTFS_VOLUME_DATA` failure, and degenerate geometry; `:231-243` treats any non-EOF `FSCTL_ENUM_USN_DATA` failure and a degenerate <8-byte reply as normal end-of-enumeration, then `:361-363` sends `ScanComplete`. `ScanCompletePayload` (Commands.h:168) has no status. Violates the protocol's own contract comment (Commands.h:33: "exactly once per scan **that runs to completion**"). Consequence: during a reconciliation pass, `VolumeSessionManager::OnScanComplete` (`:509-513`) unconditionally calls `MarkScanComplete` + `FinishReconciliationPass`, and `IndexPipeline::FinishReconciliationPass` (`IndexPipeline.cpp:939-947`) commits **every durable entry not in the pass's observed set** as `Remove` — a transient raw-open/enumeration failure therefore durably deletes a healthy volume's entire index and advances the 6 h cadence clock. | REPRODUCED → campaign |
| PA-002 | P2 | reconciliation observed set | Entries upserted via the USN journal **during** an active reconciliation pass are not recorded as seen: `ApplyMftBatchChanges` inserts into `reconciliationSeen_` (`IndexPipeline.cpp:701-717`) but `ApplyUsnBatchChanges` (`:798-859`) never touches it. `FinishReconciliationPass:939-946` then removes those rows from store + projection. The journal streams during every pass (`VolumeSessionManager.cpp:391` + reconciliation trigger paths), so files created/modified during any pass invisibly disappear until the next pass's scan re-upserts (up to ~6 h). | REPRODUCED → campaign |
| PA-003 | P2 | scan cursor/completion | Failed scan batches are never retried and completion is marked anyway: `ApplyScanBatch` (`VolumeSessionManager.cpp:483-486`) returns without touching the cursor on apply failure (comment claims a retry "can" happen), but no retry exists — the next batch carries its own cursor and `SetScanCursor` persists a position **past the un-applied records**; a failing tail batch still reaches `OnScanComplete` → `MarkScanComplete` (false durable success, silent gap until next reconciliation). Journal side (`:582-585`) likewise advances position past failed batches with no reconciliation triggered. | REPRODUCED → campaign |
| PA-004 | P2 | reconciliation trigger safety | `TriggerReconciliation` (`VolumeSessionManager.cpp:427-444`) is called unconditionally from `OnJournalOpened` (`:539`) and `OnJournalResumeInvalid` (`:616`), unlike the scheduler path (`:763` requires `scanComplete`). A journal-identity-invalid event **during the initial scan** begins a reconciliation pass mid-scan (`BeginReconciliationPass` clears the seen set, `IndexPipeline.cpp:918-922`); the still-running scan's own `ScanComplete` then finishes the pass with a partial seen set → mass false `Remove` of everything enumerated before the trigger. Double-triggering also clears a pass mid-scan. | REPRODUCED → campaign |
| PA-005 | P2 | paused-scan completion | Paused volumes drop scan batches without applying or advancing the cursor (`VolumeSessionManager.cpp:475-480`, by design), but `OnScanComplete` never checks pause state and still calls `MarkScanComplete` (`:509`) — a scan whose batches were dropped while paused is durably marked complete with permanent gaps. | REPRODUCED → campaign |
| PA-006 | P1 | durable store startup | Transient store faults are misclassified as corruption and destroy the index: `Store::RunIntegrityCheck` (`Store.cpp:294-307`) returns false for a prepare failure or **any** non-`SQLITE_ROW` step (`SQLITE_BUSY`, `SQLITE_IOERR`, `SQLITE_NOMEM`, …) exactly as for real corruption; `engine/src/Main.cpp:169-183` then unconditionally deletes `index.db` + `-wal` + `-shm` (WAL deletion discards committed-but-uncheckpointed transactions). One momentary read fault at engine start silently wipes the entire persistent index. No error-code discrimination, no retry, no backup; zero test coverage of the classification. | REPRODUCED → campaign |

Test-gap evidence: every reconciliation test (`test_index_pipeline.cpp:404/520/615/1582/1991/2066`,
`test_subtree_gating.cpp:142`, `test_seeded_indexed_mode.cpp:261`,
`test_index_pipeline_concurrency.cpp:413/500/744`) feeds evidence via *successful*
`ApplyMftBatch` calls only; none exercises a failed scan, a USN upsert between
Begin/Finish, a stale/paused completion, or a transient integrity error.

### REPRODUCED — recorded, not yet scheduled (highest-severity frontier took priority; no fixes made)

| ID | Sev | Subsystem | Finding | Status |
| -- | --- | --------- | ------- | ------ |
| PA-010 | P2 | UI file-ops shutdown | Conflict-question use-after-free: worker's `RequestConflictDecision` (`FileOperations.cpp:302-315`) frees the `question` when `stopping_` is observed without it being answered; `Stop()` is called from `WM_DESTROY` (`WindowShell.cpp:2772`) which can be dispatched *inside* the conflict dialog's nested `GetMessageW(nullptr,…)` pump (`ConflictDialog.cpp:329`, owner-disable blocks input not messages: taskbar close / `SC_CLOSE` / End-task); `PostQuitMessage` (`WindowShell.cpp:2780`) ends the pump; the `WM_APP_FILE_OPERATION_CONFLICT` handler then writes `question->mutex/decision/answered` on freed memory (`WindowShell.cpp:2326-2331`). Deterministic mechanism given the precondition. | REPRODUCED (deferred) |
| PA-011 | P2 | UI idle/reconnect | Idle-drop vs activity race permanently drops the privileged connection until the next full idle period: `IdleManager::MonitorLoop` sets `droppedForIdle_=true` (`IdleManager.cpp:66-67`), then runs the idle callback *outside* the lock (`:69-71`) which performs VACUUM (`Main.cpp:314-320`) before `DropForIdle()`. Activity during the callback resets `droppedForIdle_` and fires `RequestReconnect()` (`IdleManager.cpp:43-49`); the stale `DropForIdle()` then drops the just-reconnected connection, and no later activity can ever reconnect it (flag already false). Zero tests reference `IdleManager`. | REPRODUCED (deferred) |
| PA-012 | P2 | UI workspace persistence | Multi-window state clobber: each UI launch is a separate process (no single-instance guard, `ui/src/Main.cpp`), each holding a private `WorkspaceState` loaded once (`NavigationWorkspace.cpp:156`); `FlushState`/`SaveState` (`:712-751`) write the entire document with no reload/merge → last writer silently erases the other process's pins/recents. Shared fixed `.tmp` path (`:718`) can interleave concurrent writers; any decode/version load failure (`:565-577`) silently loads *empty* state, which the next flush persists as truth. | REPRODUCED (deferred; merge/unique-tmp fix bounded) |
| PA-013 | P2 | UI file-ops details | Failure-details viewer can never show failures: `lastOperationFailures_` is assigned only in the Progress branch (`WindowShell.cpp:2356`) but Progress events always carry an empty failures vector (`FileOperations.cpp:79,106`); terminal Completed/Cancelled events carry the real failures (`:2365`) yet never assign them. `kMenuOperationDetails` (`:2691-2707`) therefore always reports "No recent file-operation errors" after a failed batch. | REPRODUCED (deferred) |
| PA-014 | P2 | UI drag-drop | Drag-out "Move" never completes: `BeginFileDrag` discards the negotiated `DoDragDrop` result (`WindowShell.cpp:2492` ignoring `OleDragDrop.cpp:240-249`); same-volume default effect is `DROPEFFECT_MOVE` (`:251-262`); no source-deletion after a MOVE drop anywhere in the codebase → target ends with duplicates, source remains. | REPRODUCED (deferred) |
| PA-015 | P2 | clipboard | `ReadFilesFromClipboard` dereferences `GetClipboardData` returns without `GlobalLock` (`QuickActions.cpp:224,239`) while the write side correctly locks (`:180,204`); incorrect per Win32 contract — breaks against delay-rendered/serialized providers (garbage paths or AV on paste). | REPRODUCED (deferred; exploitability provider-dependent) |
| PA-016 | P2 | security hardening | `PIPE_REJECT_REMOTE_CLIENTS` absent on every `CreateNamedPipeW` call site (repo-wide zero hits): service control pipe and per-session UI pipe accept named-pipe shares over SMB. Peer authentication blocks the privileged seam remotely (client PID resolves to SYSTEM → image-path mismatch), and the UI pipe DACL is session-user-scoped, but the missing flag removes a kernel-object hardening layer the two-seam threat model relies on. UI↔engine pipe is also machine-wide per-session (not per-user) with no UI-side peer check → cross-user name-squat DoS / forged-snapshot deception (bounds-parsed → no memory unsafety). | REPRODUCED-ABSENCE (deferred; one-line flag + per-user pipe-name design decision) |

### CANDIDATE — confirmed mechanism, low severity or narrow window (recorded, not scheduled)

| ID | Sev | Finding | Status |
| -- | --- | ------- | ------ |
| PA-020 | P3 | Stale indexed merge can overwrite a fresher degraded `Success` observation (`UiServer.cpp:650-669` suppresses only prior *failures*): file reappears/disappears until the next change event. | CANDIDATE (narrow window) |
| PA-021 | P3 | Snapshot generation closed at publish before broadcast (`SnapshotPublisher.cpp:254-261` vs broadcast at `UiServer.cpp:558-583`); a UI whose `OpenFileMappingW` fails silently keeps last-known-good with no re-request (`EngineClient.cpp:444-450`). Self-heals on next publish. | CANDIDATE |
| PA-022 | P3 | `SnapshotPublisher::Stop()` (`:102-113`) mutates mapping state without `publishMutex_` vs concurrent `PublishPayload` — production order happens to serialize it; latent. | CANDIDATE |
| PA-023 | P3 | FRN sequence-reuse keys fresh MFT content under the enumerated (stale-sequence) FRN (`VolumeScanner.cpp:287-290,309`); self-heals next pass. | CANDIDATE |
| PA-024 | P3 | Skip decisions absent from completion accounting ("3 of 3" for 6 with 3 skipped): `FileOperationPolicy.cpp:96` + `FileOperations.cpp:354-355`; per-item `PostItem` counts (directory recursion) can print "147 of 3". | CANDIDATE |
| PA-025 | P3 | `cancelRequested_` reset only on empty-queue observation (`FileOperations.cpp:286-291`): an op enqueued between Esc and that observation runs pre-cancelled. | CANDIDATE |
| PA-026 | P3 | Link drop with two same-basename sources overwrites one `.lnk` (`WindowShell.cpp:1163-1169` has no intra-batch `plannedDestinations` reservation unlike `BuildTransferPlan`). | CANDIDATE |
| PA-027 | P3 | `std::async` file-type lookups capture `this` and are never joined at teardown; `hwnd_` read cross-thread vs `hwnd_=nullptr` at `WM_DESTROY` (`WindowShell.cpp:1595-1606,2779`); exit-time-only window. | CANDIDATE |
| PA-028 | P3 | `Preview.cpp:151-157`: `GetLastError()` consumed after `CloseHandle` → possible wrong error-state mapping. | CANDIDATE |
| PA-029 | P3 | Settings `.bak` preserve claim (`Settings.cpp:259-263`): `MoveFileExW` result unchecked; log can claim preservation that didn't happen. | CANDIDATE |
| PA-030 | P3 | `JsonValidator` recursion (`Settings.cpp:128-144`) has no depth cap — deep nested arrays → stack overflow at startup from a same-user file; legacy shortcut regex (`:229-243`) runs outside try/catch. | CANDIDATE (same-user trust boundary) |
| PA-031 | P3 | `UiServer::Stop()` unbounded joins (`:514-527`) and the engine park event is never signaled — the engine never executes production teardown; all shutdown-order guarantees hold only in tests. | CANDIDATE (documented reality) |
| PA-032 | P3 | Fanout head-of-line: serial `NewGeneration`/status writes can stall ≤5 s per stalled client before healthy clients (`UiServer.cpp:571-607`). | CANDIDATE |
| PA-033 | P3 | Single-slot `EngineClient` request APIs silently drop the displaced caller's callback (`EngineClient.cpp:971-1007,1056-1072`). | CANDIDATE |
| PA-034 | P3 | Engine lacks periodic service peer re-validation (one-shot at `PrivilegedConnection.cpp:211`) while service→client revalidates every heartbeat — asymmetric vs AGENTS.md "re-validated periodically" intent. | CANDIDATE |

### INVALIDATED (do not re-chase)

| ID | Hypothesis | Why invalidated |
| -- | ---------- | --------------- |
| PA-100 | File-ops worker startup race loses an enqueue kick (`FileOperations.cpp:276-279`) | Queue created at `:278` *before* the self-post at `:279`; a `PostThreadMessageW` that fails pre-queue is covered by the self-kick draining the queue. |
| PA-101 | Duplicate engines on rapid launches | Deterministic loser via `FILE_FLAG_FIRST_PIPE_INSTANCE` (`PipeListener.cpp:28-57`, engine exits at `Main.cpp:147-152`); `ERROR_PIPE_BUSY` not mislaunched (`EngineClient.cpp:100-106`). |
| PA-102 | Cross-window command misrouting / closed-tab command target | `InvokeCommand` re-resolves context + selection at invoke time (`WindowShell.cpp:825-841`); handlers take paths; no raw tab indices; per-window HWND PostMessage fanout structurally prevents connection confusion. |
| PA-103 | Home location reaching Win32 filesystem APIs | `NavigationLocation::Home()` guarantees empty path; `PanePath` returns `{}`; every command surface guards empty (`ColumnView.cpp:1070-1077` etc.). |
| PA-104 | Preview serving content through the privileged service | Preview reads in-process (`Preview.cpp:136`); no service message carries content; closed command enum intact. |
| PA-105 | Frame-length pre-allocation / batch-count cross-validation regressions | All checked: `PipeFraming.cpp:327/365-373`, `Records.cpp:10-65`, `SnapshotFormat.cpp:188-203/294-297`; u64 arithmetic, reject-not-clamp, exact-consumption. |
| PA-106 | Connection-scoped VolumeId/JournalId cross-connection Stop/Close | Enforced at `ConnectionRegistry.cpp:18/38` + `ServiceConnection.cpp:272-280/346-349`. |
| PA-107 | Rebuild older-clobbers-newer | `RebuildAll` holds `mutex_` across rebuild+prune; ingestion contends on the same mutex; cold markers retained on failure. |
| PA-108 | Rule-pruning durability regression (2026-08-28 class) | All four secondary boundaries remain fail-closed with active fault-injection regression tests (`test_index_pipeline.cpp:1991/2066`). |
| PA-109 | Palette focus race | Closed 2026-09-04 (FC-001, `b3dffc5` + `ffpalette_focus_tests`); not reopened. |
| PA-110 | Watcher overflow / deleted-while-watched leaks | 0-byte→`ERROR_NOTIFY_ENUM_DIR` re-enumerate + bounded strike give-up (`DirectoryWatcher.cpp:255-309`), tested. |
| PA-111 | Detached threads / empty catches / unguarded Sleep-readiness sweeping the tree | Zero `.detach()`; reviewed `catch(...)` sites deliberate; readiness `Sleep`s bounded with state predicates. |
| PA-112 | Snapshot payload OOB read from header lie at map time | `EngineClient.cpp:47-68` validates payload against `VirtualQuery` region at map; residual re-read window (N-5) recorded as CANDIDATE-class note only. |

### Wave coverage

startup/lifecycle AUDITED · index durability AUDITED · MFT/USN sequencing AUDITED ·
degraded mode AUDITED · search AUDITED (semantics/generation gating clean; no fixture defect found) ·
file operations AUDITED · preview/properties AUDITED · navigation AUDITED ·
Home/recents/pins AUDITED · command routing AUDITED · shell integration AUDITED ·
multi-window/tab AUDITED · cancellation/supersession AUDITED · security boundaries AUDITED ·
parsers AUDITED (incl. fuzz-gap enumeration) · performance AUDITED (no measured
regression pursued; fanout latency PA-032 recorded) · error semantics AUDITED ·
crash/restart AUDITED (static + fixture-level; process-kill matrix not executed) ·
test blind spots AUDITED (matrix below).

NOT EXECUTED this session: live multi-process soak, real process-kill fault matrix,
privileged execution (external by policy), ASAN re-run (no parser changes until
the campaign build).

### Test blind-spot highlights (Wave S)

1. Process-lifecycle crossing: no test spawns/kills real engine/service.
2. Destructive DB fallback path: zero coverage (→ PA-006, campaign requirement).
3. Reconciliation completion truthfulness: all tests feed *successful* MFT batches only (→ PA-001..PA-005, campaign).
4. Shell integration real paths (clipboard/drag effects): pure helpers only.
5. Wire-level supersession/cancel across processes.
6. Multi-client engine (N UI ↔ 1 engine).
7. `ApplyFixupAndValidate`/`ParseMftAttributes` and `DecodeUsnRecord` have no randomized fuzz targets (fixed fixtures only) — recommended follow-up.

## Outcome

Highest-severity reproduced frontier selected: **durable-index false-success**
(PA-001..PA-006) → campaign `index-durability-false-success-closure` (active in
`openspec/lifecycle.json`). All other reproduced P2s recorded above with exact
evidence for follow-on bounded slices; nothing else fixed in this session.
