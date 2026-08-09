# Fast Files — Frontier Recovery Handoff

> Handoff written 2026-08-08 (Asia/Manila) for a fresh coding agent/session.
> The active user objective remains: resolve all with the ultimate objective of making FastFiles usable.
> This is a continuation aid, not an assertion that the product is complete.

## 0. Read this first

The repository is intentionally dirty and contains valuable uncommitted recovery work. The previous agent was doing implementation after a repository-wide forensic Explore. Continue from the working tree as it is; do not reset it to HEAD and do not discard anything merely because it looks unrelated.

The most important recent progress is real runtime recovery of the development layout:

* FastFiles.exe can discover and launch the sibling development-tree FastFilesEngine.exe.
* The UI connects, subscribes, maps the read-only snapshot, and can request directory data.
* A safe runtime smoke demonstrated root/deeper navigation state changes, not merely process startup.
* Directory watcher lifecycle and snapshot-reader correctness were hardened and have focused tests.
* Search candidate materialization is cached per snapshot generation and UI results are bounded/truthfully marked as truncated.
* Settings now affect category filtering and startup/session location persistence.
* Degraded enumeration now distinguishes “not found/access denied” from a generic I/O/network failure.

These changes are not a reason to mark the recovery OpenSpec change complete. The recovery tasks remain unchecked by design; formal runtime acceptance and the broader product gaps still need work.

## 1. Hard repository rules

Do not run any of the following:

~~~
git reset / git reset --hard / git checkout -- . / git restore
git clean / git stash / git revert / git rebase / git merge
git cherry-pick / git switch / git commit
~~~

Do not delete untracked reports, .obj diagnostics, agent directories, OpenSpec drafts, or generated evidence. Do not auto-format or regenerate sources. Use apply_patch for edits. Build tools are acceptable only when configured not to rewrite source; build outputs under build/ are disposable but should not be manually cleaned.

Read AGENTS.md before changing architecture. In particular, the three-process security split, the closed service protocol, and the fail-closed signature-pin policy are invariants, not optional implementation details.

Never populate the all-zero Authenticode pins in src/setup/include/ffsetup/PinnedSignatures.h, bypass peer authentication, add a generic privileged path/handle primitive, or weaken ACL/path/DLL hardening. The real privileged MFT/USN path is intentionally not release-validated until genuine pins and operational signing evidence exist.

## 2. Objective and working mode

The objective is not “make tests green.” It is to move Fast Files toward a genuinely usable daily file manager while preserving the existing engineering investment. Every future slice must distinguish:

~~~
code exists  ->  code is connected  ->  code runs  ->  feature is user-usable
~~~

Do not claim an indexed-search, MFT, storage, shell-integration, or production-service feature merely because a class or unit test exists.

The previous large forensic report is represented by FASTFILES_RECOVERY_REPORT.md and ENGINEERING_AUDIT_REPORT.md (both untracked). Read them for evidence, but prefer current code and fresh verification over prose.

## 3. Git/state baseline known at handoff

Last authoritative repository facts from the preceding turn:

* Branch: main.
* HEAD: 52d33da (Align docs, tooling, and tests with the shipped architecture).
* Upstream was origin/main at the same commit.
* Nothing was staged by the recovery session.
* The original salvage baseline was approximately 33 modified tracked files (452 insertions, 140 deletions). After subsequent implementation slices, the working tree had approximately 51 tracked modified files and substantially more line delta. Refresh the exact counts with git status --short, git diff --stat, and git diff --cached --stat in the new session; do not assume the figures are exact.
* Untracked valuable material includes, at minimum, src/ui/src/EngineDiscovery.cpp, src/ui/src/EngineDiscovery.h, tests/ui/test_engine_discovery.cpp, openspec/changes/recovery-degraded-browsing/, openspec/changes/address-engineering-audit-findings/, FASTFILES_RECOVERY_REPORT.md, ENGINEERING_AUDIT_REPORT.md, .agent/, .gemini/, .opencode/, and several ff_*.obj diagnostic artifacts.
* OpenSpec/tooling commands may refresh generated agent-integration file mtimes. Treat .claude/, .cline/, .codex/, .cursor/, .kimi/, .mcp.json, and similar documentation/tooling deltas separately from product salvage; do not silently erase them.

First commands in a new session:

~~~powershell
git status --short --branch
git diff --stat
git diff --cached --stat
git log --oneline --decorate -12
git reflog -12
git ls-files --others --exclude-standard
~~~

Make a WIP checkpoint only if the user later authorizes a Git mutation. Until then, preserve the working tree in place.

## 4. Toolchain and verification recipe

The ordinary shell in the last session did not always have cmake, MSVC include paths, or Windows SDK library paths. The reliable recipe is to call the VS developer batch file and run CMake in the same cmd.exe environment:

~~~powershell
$vs='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$cmd = 'call "' + $vs + '" -arch=x64 && "' + $cmake + '" --build --preset debug'
cmd.exe /d /c $cmd
~~~

The matching analysis build is:

~~~powershell
$cmd = 'call "' + $vs + '" -arch=x64 && "' + $cmake + '" --build --preset analyze'
cmd.exe /d /c $cmd
~~~

The final successful verification before this handoff was:

* Debug build: success; all targets linked.
* /analyze build: success after the latest search changes; no reported static-analysis failure.
* Serial CTest: 34/34 passed in about 26 seconds. This included protocol fuzz/diagnostics, index store, search, parser, engine, watcher, snapshot, navigation, file-operation E2E, UI helpers, PowerShell harness tests, setup security descriptors, and doc drift.
* Focused search tests (ffsearch_tests, ffsearch_scan_tests): both passed.
* Runtime smoke: launched build/debug/src/ui/FastFiles.exe; UI was responsive with a real window and the discovered engine path was D:\Documents\tryPython\FastFiles\build\debug\src\engine\FastFilesEngine.exe. The UI process and the exact engine process were stopped by their process objects after the smoke. No user files were touched.

Re-run all three gates after any new code:

~~~powershell
ctest --test-dir build/debug --output-on-failure
git diff --check
~~~

Use pwsh ./verify/verify.ps1 build and doctor only as read-only verification when needed. Never read or glob verify/runs/, verify/baselines/, or verify/.signing/; those are execution evidence/signing material and are explicitly out of scope.

## 5. Current implementation ledger

### 5.1 Engine discovery and connection

Relevant files:

* src/ui/src/EngineDiscovery.cpp, src/ui/src/EngineDiscovery.h
* src/ui/src/EngineClient.cpp, src/ui/src/EngineClient.h
* tests/ui/test_engine_discovery.cpp

EngineDiscoveryCandidates tries, in order, the UI directory (installed/staged layout), the normalized sibling ..\engine\FastFilesEngine.exe development layout, and %ProgramFiles%\FastFiles\FastFilesEngine.exe. IsValidEngineExecutable rejects missing paths and directories. EngineClient::LaunchEngineIfNotRunning validates each candidate before CreateProcessW, logs launch failures, and records a terminal engine-not-found diagnostic. Do not revert this to UI-directory-only.

EngineClient now has bounded reconnect state (Starting, Connected, Unavailable), retains queued directory requests until a frame is successfully written, and does not permanently lose a request just because the pipe was not yet valid. It polls the UI pipe with PeekNamedPipe before synchronous frame reads to avoid a same-handle blocking deadlock. The UI connection and subscription path are runtime-proven; the exact mouse-to-request edge still deserves an automated acceptance seam.

Known remaining risk: launch validation is a regular-file check, not a complete Authenticode/path-policy validation. The privileged service’s symmetric image/signature policy must remain independent and fail closed.

### 5.2 Snapshot publication and reading

Relevant files:

* src/protocol/include/ffprotocol/SnapshotFormat.h
* src/protocol/src/SnapshotFormat.cpp
* src/engine/src/SnapshotPublisher.cpp, SnapshotPublisher.h
* src/ui/src/EngineClient.cpp
* tests/engine/test_snapshot_publisher.cpp

The section is memory mapped and double buffered with two 8 MiB slots. The header contains generation, active slot, and per-slot data sizes. The UI maps with FILE_MAP_READ; LoadSharedU32/LoadSharedU64 performs a memory barrier followed by a volatile read. Do not use an interlocked read/write operation on the read-only mapping: an earlier version faulted because InterlockedCompareExchange writes even when the value compares equal.

The reader validates slot indices and sizes, then retries a bounded number of times if generation/slot/size changes during parsing. The publisher resets generation when starting a fresh mapping. The focused test verifies descriptor bounds, slot flips, generation progression, parseability, and restart behavior.

Remaining architectural issue: the 8 MiB per-slot capacity is still a real scale ceiling. It has not been redesigned. The snapshot header’s raw cross-process fields are not C++ atomics; the current barrier/retry approach reduces torn-read risk but is not a substitute for a deliberately specified cross-process publication protocol. Do not claim the snapshot is scalable to millions of entries.

### 5.3 UI server and degraded browsing

Relevant files:

* src/engine/src/UiServer.cpp, UiServer.h
* src/engine/src/DegradedModeEnumerator.cpp, DegradedModeEnumerator.h
* src/engine/src/DirectoryWatcher.cpp, DirectoryWatcher.h
* src/protocol/include/ffprotocol/UiProtocol.h, SnapshotFormat.h
* src/ui/src/ColumnView.cpp, ColumnView.h
* tests/engine/test_directory_watcher.cpp, test_degraded_special_files.cpp

UiServer::HandleRequestDirectory enumerates through the unprivileged degraded path, publishes the updated directory snapshot, and reports publish/enumeration failures instead of silently leaving stale data. MergeIndexDirectories canonicalizes incoming C: to C:\ at the UI-server merge boundary while leaving the existing IndexPipeline test contract untouched.

The enumerator reports success, access denied, not found, unsupported/device errors, and a new generic I/O/network error. Non-ERROR_NO_MORE_FILES termination from FindNextFileW is not falsely reported as success. The UI maps the generic error to a visible I/O or network error state.

The watcher now uses overlapped ReadDirectoryChangesW, explicit stop and I/O events, CancelIoEx, wait/join, and deterministic handle cleanup. Roots are bounded, duplicate roots are refreshed, stale roots are LRU-evicted, and dead entries are stopped before replacement. Overflow/zero-byte notifications cause re-enumeration and re-arm; repeated failures reach an eight-strike cap. The test watches a temporary directory, observes a file, unwatch/re-watches, observes again, and calls UnwatchAll.

Remaining watcher risk: callback work can still perform enumeration/publish synchronously, and StopEntry waits for callback completion. This is much safer than the former joinable-thread replacement/termination hazard but is not yet a fully backpressured producer/consumer design. Broadcasts and engine shutdown need further stress validation.

### 5.4 Column View and navigation state

Relevant files:

* src/ui/src/ColumnView.cpp, ColumnView.h
* src/ui/src/WindowShell.cpp, WindowShell.h
* src/ui/src/NavigationWorkspace.cpp, NavigationWorkspace.h
* src/ui/src/NavigationChrome.cpp
* tests/navigation/test_navigation_workspace.cpp

The synthetic root drive row is a directory and correctly produces C:\; ActivateItem leads to RequestColumn and EngineClient::RequestDirectory. Runtime smoke observed generation/slot changes after root/deeper clicks. DPI coordinates are normalized before DIP hit testing, dual-pane commands are wired to actual view state, inactive tab buttons are no longer permanently disabled, and category/storage notifications route through the shell.

Selection preservation must be treated carefully: old numeric-index behavior could retarget a selection after watcher reorder/insertion/deletion, causing copy/delete/move to hit a different path. The current work includes identity/path-oriented preservation; re-check the exact current implementation before touching it.

Startup/session state now persists lastLocation; NavigationWorkspace::ParseQuoted handles an even/odd run of trailing backslashes correctly, so C:\ does not corrupt JSON parsing. WindowShell::Initialize consumes a valid startup location or the previous-session location and navigates there. The default synthetic This PC value is not treated as a filesystem path.

Important remaining UI verification gaps from the earlier forensic inspection:

* ColumnView now has active-pane vertical offsets, wheel scrolling, and
  keyboard reveal (`ColumnView::ScrollVertical` /
  `EnsureFocusedItemVisible`); this is statically covered but still deserves a
  real long-directory smoke.
* Main-browser Up/Backspace behavior is implemented through the shared
  `navigation.up` command, but only Backspace/deep-navigation evidence exists;
  chrome Back/Forward button behavior still needs a multi-folder runtime proof.
* The details-pane layout/hit-test overlap finding was addressed by the pure
  `DetailsLayout` seam and the shell's `NavigationViewportWidth`; the 100% and
  150% geometry tests pass, but visual interactive-DPI evidence is still open.
* Tab restoration and dual-pane state have better wiring but still need runtime
  acceptance, persistence, and independent pane-navigation verification.

### 5.5 Search

Relevant files:

* src/search/include/ffsearch/Search.h
* src/search/src/Search.cpp
* src/ui/src/SearchPanel.cpp, SearchPanel.h
* tests/search/test_search.cpp

SearchRequest::maxResults and SearchResponse::truncated bound UI result memory/work. ExecuteSearch stops at the cap and sorts the retained results; SearchPanel reports that only the first N results are shown. The search panel materializes candidates from the current snapshot once per snapshot generation and invalidates the cache on WM_APP_SNAPSHOT_UPDATED, avoiding a full snapshot-to-candidate rebuild on every keystroke. The cache is mutex-protected and avoids retaining a build that raced a generation change.

This is a latency/memory improvement, not a persistent-index implementation. Search still operates on the UI’s degraded/browsed snapshot and its labels honestly say Current Drive — Browsed Folders Only and All Browsed Locations. It does not provide whole-volume MFT/SQLite search, and no engine query seam has yet been added. A future indexed-search change must not silently relabel the existing snapshot search as all indexed locations.

Potential next seam: expose a read-only engine/index query operation (or an index snapshot designed for broad search) with explicit scope, cancellation, result cap, truncation, index availability, and generation/availability state. Keep keystroke-local snapshot search as the low-latency fallback.

### 5.6 Storage analysis and treemap

Relevant files:

* src/ui/src/StorageAnalysis.cpp, StorageAnalysis.h
* src/ui/src/TreemapView.cpp
* src/ui/src/WindowShell.cpp
* src/ui/include/ffui/StoragePercentage.h
* tests/ui/test_storage_percentage.cpp

Category settings are now pushed into StorageAnalysis::SetCategories both at initialization and after successful settings saves. The prior parent-percentage denominator defect and overflow risk were addressed in the dirty working tree, and percentage helper tests pass. Storage WM_NOTIFY/owner-data routing and aggregate request-ID handling were repaired in earlier work; callbacks no longer compare a global engine request ID to an unrelated local 1..N counter.

The treemap layout math itself has tests and is not the primary blocker. Data plumbing remains incomplete/partially verified: directory rows previously received size zero/calculating values, recursive aggregate requests and parent totals need real runtime evidence, and a full overview → aggregate → list → treemap vertical has not been accepted. The next storage slice should add deterministic, non-destructive integration coverage for aggregate data and visible treemap values, not merely more squarification tests.

### 5.7 Persistent index and privileged path

Relevant areas:

* src/indexstore/ (SQLite store, projection, NamePool/FlatHashMap)
* src/engine/src/IndexPipeline.cpp, VolumeSessionManager.cpp
* src/indexsvc/src/VolumeScanner.cpp, MftParser.cpp, UsnJournalReader.cpp, ServiceConnection.cpp
* src/setup/include/ffsetup/PinnedSignatures.h

The MFT/USN scanner, parser, journal reader, service protocol, ingestion, SQLite store, projection, and snapshot export code exist and have meaningful parser/unit coverage. They have not been validated end-to-end under the real mutually authenticated service because the signature pins are all-zero placeholders. Treat the privileged pipeline as implemented but operationally inactive/unverified, not as working product functionality.

The constrained LocalSystem broker model is deliberate: closed commands, connection-scoped opaque handles, strict frame/batch validation, symmetric path+signature authentication, no raw file-content forwarding, startup privilege verification, ACL hardening, and DLL search hardening. Preserve these. The untracked openspec/changes/address-engineering-audit-findings/ draft is not implementation-ready: it contains malformed task syntax, assumes an overlapped pipe where the listener is synchronous, misstates atomic generation, proposes unsafe development-auth ideas, and should remain an audit draft.

Known index risks still outstanding: fixed snapshot capacity, whole-index export cost, possible O(N²)-like initial ingestion, projection/name-pool/mapping memory growth, SQLite migration/versioning depth, journal wrap/recreation/reconciliation behavior, hard-link/rename/move/delete edge cases, volume replacement/offline handling, and coarse pipeline locking. Do not redesign these opportunistically while fixing UI navigation.

### 5.8 File operations, preview, commands, settings, renderer

The file-operation subsystem has comparatively strong unit and E2E coverage (including a temporary-directory E2E). Preserve its STA/IFileOperation architecture and do not route privileged indexing into file operations. Explorer parity remains partial: properties/context menus/clipboard/shell integration and some undo semantics are not equivalent to a daily Explorer-class manager.

Preview has text/image/WIC paths and cancellation-related tests, but rapid-selection and resource/device-loss stress remain worth checking. Command registration/palette/menu/handler coverage is broad but contains dead or duplicate commands; settings have historically included write-only values. Recent startup/category wiring improves this, but a complete setting read/write/effect matrix has not been re-audited after the latest edits.

The renderer uses the intended Win32/D3D11/Direct2D/DirectWrite/DirectComposition stack and shared UI tokens. Device-loss recovery exists. Do not propose a framework migration without evidence. Manifest/long-path awareness and network/reparse/shell edge cases remain release concerns.

## 6. OpenSpec truth at handoff

openspec/changes/recovery-degraded-browsing/tasks.md remains all unchecked. Static evidence supports much of tasks 1–6 (discovery, bounded reconnect, status/retry UI, publish-failure surfacing, watcher bounds/overflow, honest scope labels), and tests/runtime support some acceptance, but tasks must not be checked until the change’s own acceptance is demonstrated and evidence is recorded.

openspec/changes/close-independent-validation-gaps/tasks.md is the current broader open-work source of truth; many tasks remain open. Do not archive or sync changes during this continuation unless explicitly requested and all gates are genuinely met.

The foundation architecture change and most older changes are historically closed, but “closed” does not imply every promised feature is currently usable. The forensic matrix found multiple overclaims/underclaims, especially around persistent search, service operation, storage plumbing, tabs, shell parity, and runtime acceptance.

The recovery change should likely be finished first as a narrow degraded-browsing vertical, then a new indexed-search/storage sequence should be proposed. Do not edit task checkboxes as a substitute for implementation.

## 7. Known runtime truth

Evidence levels:

* Confirmed by build/tests: 34 CTest targets pass serially; /W4 /WX debug and /analyze builds pass in the correct VS developer environment.
* Confirmed by manual process smoke: UI launches responsively; Engine discovery launches the exact sibling development-tree engine; UI/engine process pair exists and can be cleanly stopped; prior P/Invoke smoke produced RootClick=True, then DeepClick=True, with snapshot generation/slot changes (AfterRoot ... generation 3327, AfterDeep ... generation 3661 in that run).
* Not yet a formal acceptance test: the complete input chain from a physical/UIA mouse click through ColumnView::OnMouseDown, request queue, pipe frame, UiServer::HandleRequestDirectory, snapshot publication, notification, remap, refresh, and render. Diagnostics previously showed REQUEST_DIR_COUNT=0 in a run that did not exercise the corrected click seam; do not treat old zero-count evidence as current failure, but add a real automated seam.
* Not verified: five-level interactive navigation, Up/Backspace, watcher-visible new-file update in the UI, broad search outside browsed roots, storage treemap values, tabs/dual pane persistence, shell properties/context menu, long paths/UNC, service/MFT/USN end-to-end, production/install signing.

## 8. Exact implementation plan for the next agent

Do not jump to a giant rewrite. Use small, vertically accepted changes.

### Slice A — finish degraded browsing acceptance (highest immediate priority)

1. Inspect current ColumnView/WindowShell event routing and add a testable seam or existing UIA action that exercises a real drive-row click and a folder-row click.
2. Capture evidence at each edge: mouse/UIA input, activation, generated path, RequestDirectory call, successful pipe write, server handler, enumeration status, publish generation, UI notification/remap, and rendered row count.
3. Add a safe temporary-directory acceptance path for empty, normal, inaccessible, and I/O-error directories without touching user data.
4. Verify five-level navigation, Back, Up, engine disconnect/status, watcher update, and reconnect. Keep diagnostics bounded and remove only debug instrumentation after replacement with production telemetry—do not delete evidence files.
5. Only after this evidence exists, update the recovery change bookkeeping in a separately authorized OpenSpec action.

### Slice B — storage analyzer vertical

1. Trace the current StorageAnalysis request/response IDs and TreemapView data model from WindowShell command through aggregate callback.
2. Add a deterministic projection/aggregate fixture test that proves directory totals, parent totals, % of parent, largest-file/dir rows, category filter, and treemap rectangles receive nonzero directory sizes.
3. Add runtime UI acceptance using a private temp tree or a synthetic engine fixture. Verify WM_NOTIFY owner-data text, activation/drill-down, sorting, and treemap redraw. Do not call the algorithm broken if only plumbing fails.
4. Bound large aggregate work and make cancellation/shutdown explicit before broadening scope.

### Slice C — honest indexed-search vertical

1. Decide the source of truth: SQLite query in the engine versus a larger/dedicated immutable index snapshot. The current UI snapshot is browsed/degraded data and cannot be relabeled.
2. Define a narrow, read-only, cancellable query seam with explicit SearchScope, root, sort, result cap, truncation, index availability, and generation. Preserve the closed privileged service command surface; the UI↔engine control plane is not the privilege boundary.
3. Add engine/search integration tests against a temporary SQLite database and a small projection, including cancellation, empty/stale index, scope boundaries, Unicode/case behavior, and cap/truncation.
4. Connect the panel only after the seam is tested; retain current in-process snapshot fallback and honest labels when the index is unavailable.
5. Measure keystroke latency and UI-thread blocking; never parse a whole snapshot or build unbounded candidates on the UI thread.

### Slice D — state/shell parity

After A–C: add identity-based selection tests, vertical scrolling/keyboard reveal, Up/Backspace, details-panel layout exclusion, tab restore, dual-pane independent state, clipboard/Explorer properties/context menu, long-path manifest, and network/reparse behavior as separate changes.

### Slice E — privileged production gate

Only after degraded daily use and indexed verticals are stable: obtain real release/test pins through the authorized signing process, run clean-host/elevated service validation, MFT/USN reconciliation tests, journal wrap/recreation tests, and installer rollback evidence. Never solve this by bypassing authentication.

## 9. Acceptance checklist for a usable milestone

Maintain this table with evidence, not assumptions:

| Acceptance | Current state | Required proof |
|---|---|---|
| UI launches | PASS | fresh process/window smoke |
| Development engine launches | PASS | exact sibling path and process evidence |
| UI subscribes/maps snapshot | PASS/partial | diagnostics plus reader test |
| Drive list appears | runtime likely, formal UIA pending | UIA or deterministic harness |
| Click drive populates next column | runtime smoke indicated, formal edge pending | real input-to-handler trace |
| Navigate five levels | NOT VERIFIED | temp tree UI acceptance |
| Back works | NOT VERIFIED | same acceptance |
| Up/Backspace works | NOT VERIFIED/known gap | input test + runtime |
| Watcher new file appears | watcher unit PASS; UI E2E pending | temp tree + generation/render evidence |
| Search browsed item | likely PASS; focused tests PASS | runtime panel evidence |
| Search arbitrary unbrowsed volume | FAIL/not implemented | indexed-search vertical |
| Storage overview/list | partial | aggregate fixture + UI runtime |
| Treemap directory sizes | partial/previously zero | nonzero data plumbing evidence |
| Copy/rename/delete temp item | file-op E2E PASS | retain existing E2E, add UI route if needed |
| Engine failure visible/recoverable | partial | kill/restart controlled process test |
| MFT/USN persistent index | code exists, NOT runtime verified | signed service integration |
| Tabs/dual pane/session restore | partial | runtime state matrix |
| Properties/shell parity | partial/stub | explicit feature decision and tests |
| Long paths/UNC/reparse | NOT VERIFIED | platform fixture and manifest evidence |

## 10. Important symbols/files to inspect first

1. src/ui/src/EngineClient.cpp — LaunchEngineIfNotRunning, reconnect loop, SendDirectoryRequest, ReadSnapshot.
2. src/ui/src/EngineDiscovery.cpp — candidate order and executable validation.
3. src/engine/src/UiServer.cpp — HandleRequestDirectory, merge/publish/error broadcast.
4. src/engine/src/DegradedModeEnumerator.cpp — status classification and FindFirstFileExW loop.
5. src/engine/src/DirectoryWatcher.cpp — overlapped lifecycle, overflow, LRU, stop/join.
6. src/engine/src/SnapshotPublisher.cpp — slot publication and generation ordering.
7. src/protocol/include/ffprotocol/SnapshotFormat.h — shared header/capacity/status contract.
8. src/ui/src/ColumnView.cpp — root row, hit test, activation, refresh/selection, scrolling.
9. src/ui/src/WindowShell.cpp — message routing, DPI conversion, details layout, startup state, storage notifications.
10. src/ui/src/NavigationWorkspace.cpp — path/history/session serialization.
11. src/ui/src/SearchPanel.cpp — candidate cache, dispatch, scope labels, result cap/status.
12. src/search/src/Search.cpp — scope filtering, matching, sorting, cancellation/cap.
13. src/ui/src/StorageAnalysis.cpp — aggregate request IDs, owner-data handlers, category filters.
14. src/ui/src/TreemapView.cpp — data model versus layout math.
15. src/indexstore/src/Store.cpp and projection sources — SQLite/scale semantics.
16. src/indexsvc/src/ServiceConnection.cpp — synchronous pipe/auth/service boundary.
17. src/setup/include/ffsetup/PinnedSignatures.h — all-zero fail-closed pins; protected.
18. openspec/changes/recovery-degraded-browsing/ — current recovery scope/tasks/spec.
19. openspec/changes/close-independent-validation-gaps/ — broader open work.
20. verify/verify.ps1 and verify/uia-driver/ — existing safe verification capabilities; do not read forbidden evidence folders.

## 11. Mistakes/failed approaches to avoid repeating

* Do not use an interlocked compare-exchange to read a read-only cross-process mapping; it writes and crashed the UI.
* Do not canonicalize IndexPipeline’s root C: contract in a way that breaks existing index-pipeline tests; canonicalization belongs at the UI-server merge boundary unless the contract is deliberately changed everywhere.
* Do not assume a synchronous named pipe supports overlapped completion semantics; PipeListener.cpp creates a synchronous pipe. The audit draft’s overlapped-pipe premise is wrong.
* Do not make SnapshotPublisher::generation_ atomic as if that alone solved a process-local/shared-memory publication race; it is already atomic locally. The actual issue is cross-process header ordering and reader retry.
* Do not silently drop directory requests while connecting. Retain until a successful frame write or explicitly report cancellation.
* Do not call snapshot search all indexed locations when it only contains browsed degraded directories.
* Do not mark watcher overflow successful if FindNextFileW terminated with an error other than ERROR_NO_MORE_FILES.
* Do not replace joinable watcher entries in a container without stopping/joining them first; that caused a potential std::terminate path.
* Do not fix storage by changing squarification math when the real failure is missing aggregate data/routing.
* Do not use broad process-name kills in runtime tests; stop only the exact process object/path started by the test.
* Do not run OpenSpec commands that auto-sync generated tooling and then mistake mtime changes for product work.

## 12. Final transfer instruction

A fresh agent should begin by reading this file, AGENTS.md, and the current git diff for the symbols above. Re-run the status/build/test gates before editing. Then implement Slice A (formal degraded browsing acceptance) or, if that seam is already proven in the current tree, Slice B (storage vertical) as one bounded change. Keep the recovery goal active. Preserve the security boundary and all dirty work. The correct end state is a usable application with evidence-backed OpenSpec truth, not merely another green unit-test pass.

## 13. Handoff update after this document was written

The next continuation turn found and fixed a real dual-pane input defect in the
dirty tree. `ColumnView::OnMouseDown` and `OnMouseMove` previously divided the
window-relative x coordinate by the column width even after switching to pane 1;
right-pane clicks therefore targeted a nonexistent far-right column. The pure
`ComputeColumnHitTest` seam in `src/ui/src/ColumnView.h` now subtracts the active
pane origin and applies horizontal/vertical offsets consistently. The renderer
and hit-test paths use the same calculation. Regression coverage is in
`tests/ui/test_column_hit_test.cpp`, registered as `ffcolumn_hit_test_tests`.

Fresh verification after that change:

* Debug build: PASS.
* `/analyze` build: PASS.
* Serial CTest: **35/35 PASS**, including `ffcolumn_hit_test_tests` and
  `ffdoc_drift_tests`.
* Fresh launch smoke: UI process responsive; exact development-tree engine
  discovered and responsive; both processes stopped by their process objects.
* `git diff --check`: no content errors (the existing CRLF warnings for older
  dirty files remain normal).

The existing `windows-ui-automation-validation` capability was also invoked;
it returned `FAIL scenario-failed` and only printed its run-tree path. Do not
read the forbidden generated run artifacts. The failure is not yet diagnosed
from evidence, so do not use that result to claim the application’s navigation
is broken. The next agent should either run a direct controlled UIA scenario
with in-memory subresults or add a safer deterministic acceptance seam.

## 14. Handoff update: storage aggregate plumbing

The next implementation slice also wired treemap directory nodes to the
existing asynchronous `EngineClient::RequestFolderAggregate` seam. Changes are
in `src/ui/include/ffui/TreemapLayout.h`, `src/ui/src/TreemapView.h/.cpp`,
`src/ui/src/StorageAnalysis.h/.cpp`, and `src/ui/src/WindowShell.cpp`.

Treemap nodes now retain volume/FRN metadata, request aggregates only for
unknown directories that have usable indexed identity, and receive results on
the UI thread through `WM_APP_TREEMAP_AGGREGATE`. Responses are guarded by a
monotonic tree-build generation and a shared lifetime token, so a late result
cannot mutate a hidden/replaced/destroyed tree. Resolved totals recompute
unresolved parent totals and rebuild the layout; failed responses remain
visually calculating rather than becoming misleading zero-byte “known” tiles.
Degraded snapshots whose entries have zero volume/FRN metadata still correctly
remain partial/calculating; this wiring does not pretend degraded enumeration
is a persistent index.

Fresh verification after the storage plumbing:

* Debug build: PASS.
* `/analyze` build: PASS.
* Serial CTest: **35/35 PASS**.
* Fresh UI/engine process smoke: both responsive and cleanly stopped.

## 15. Runtime revalidation after the storage slice

A controlled Win32 message smoke was repeated against the current debug
binary. The UI process created a real `FastFiles` window, the sibling engine
process was launched and responsive, and a posted root-drive click was
accepted by the window. Existing diagnostics contain a complete successful
runtime chain at timestamp `1786177796`:

~~~text
dbg-ui-mousedown
dbg-column-hit
dbg-column-activate path=C:\
dbg-request-queued path=C:\
dbg-request-sent path=C:\
dbg-request-dir path=C:\
dbg-republish-published
~~~

This proves the formerly missing request edge at least once with the current
binary. It does not prove five-level navigation, watcher-to-render update, or
formal UIA acceptance. A separate cold-launch smoke also reproduced an
intermittent `engine-launched` followed by `engine-connect-failed error=0x2`
cycle before cleanup. Treat that as a bounded-startup/reconnect acceptance
risk: the engine process and UI can both be responsive while the UI remains
unable to complete `ConnectAndSubscribe` during the attempt window. Do not
classify this as a security failure or bypass it; next work should identify
whether the failure is pipe readiness, snapshot mapping, or startup ordering
(notably the engine's best-effort scheduled-task registration before the UI
server's long-lived park).

## 16. Startup and preview reliability slice

The next continuation resolved the two reproducible reliability problems above.

`src/engine/src/Main.cpp` now starts `UiServer` before performing the
best-effort Scheduled Task self-healing call. The task registration remains
unchanged and fail-soft, but a cold Task Scheduler COM delay can no longer
hide the UI pipe. `src/ui/src/EngineClient.cpp` now records stage-specific
connection diagnostics, preserves the original Win32 error for the aggregate
record, and polls for a just-launched named pipe for a bounded ten-second
window (because `WaitNamedPipeW` returns immediately when no instance exists).

`src/ui/src/Preview.cpp` now drops queued obsolete preview work whenever a new
selection arrives or the controller is cleared. Running workers still use
cancellation and request identity, with a short completion coalescing window
to prevent a fast obsolete file from reaching the UI between two selection
messages.

Verification after the slice:

* Debug build: PASS.
* `/analyze` build: PASS.
* Serial CTest: **35/35 PASS**.
* `ffpreview_tests` repeated 30 times: PASS (30/30).
* Three fresh UI launches: each UI and engine responsive; each diagnostics
  chain was `engine-launched` followed directly by `engine-connect-succeeded`.
* `ffdoc_drift_tests`: PASS after normalizing `Main.cpp` to CRLF.

The broad product gaps remain: indexed all-volume search is not wired, the
privileged service is still fail-closed behind placeholder pins, formal UIA
navigation remains unverified, and five-level/back/up/watcher E2E acceptance
is still pending.

## 17. Watcher completion and runtime boundary evidence

`src/engine/src/DirectoryWatcher.cpp` was corrected to read completion bytes
from `GetOverlappedResult` for every overlapped arm, including a `TRUE`
`ReadDirectoryChangesW` return. The old path classified the initial
successful asynchronous arm's untouched `bytesReturned == 0` as
`ERROR_NOTIFY_ENUM_DIR`; this produced eight false overflow records and
`watch-gave-up` immediately for drive roots. The corrected path still waits on
the stop event plus I/O event, remains cancellation-safe, and preserves the
real overflow/re-enumerate bound for genuine buffer exhaustion.

Focused `ffengine_directory_watcher_tests` passed after the correction. Full
serial CTest passed **35/35**, including doc-drift after normalizing the two
modified engine sources to CRLF. Debug and `/analyze` builds passed.

Production diagnostics were added at the UI/engine request seam (these are
state-transition records, not temporary `dbg-` tracing):

* `src/ui/src/EngineClient.cpp`: `ui-directory-request-queued` and
  `ui-directory-request-sent`.
* `src/engine/src/UiServer.cpp`: `engine-directory-request-received`.

The fresh runtime trace at timestamp `1786188270`/`1786188272` showed the
development-tree engine launched, `engine-connect-succeeded`, and real
sidebar drive requests for `D:\` and `C:\` queued, sent, and received. No new
false watcher-overflow records appeared for those requests during the next
five seconds. This proves the UI child-sidebar input -> queue -> pipe ->
engine dispatch edge and the watcher fix in a real process, but not yet
multi-level navigation or a watcher-created-file render assertion.

The current production launch should therefore be treated as: startup,
engine discovery/connection, drive-list/sidebar input, directory request,
degraded enumeration, snapshot publication, and basic rendering are
runtime-demonstrated; indexed broad search, service-authenticated indexing,
formal UIA, and multi-level acceptance remain open.

## 18. Logical-drive root coverage

`src/ui/src/ColumnView.cpp::ColumnView::Initialize` no longer filters the
synthetic drive-root column to `DRIVE_FIXED` only. It now admits every logical
drive type except `DRIVE_UNKNOWN` and `DRIVE_NO_ROOT_DIR` (fixed, removable,
optical, RAM-disk, and mapped network roots). The sidebar already enumerated
all logical drives, so this removes a UI inconsistency: a removable/network
drive shown by the sidebar can now also be represented in the Finder-style
root column. Actual access failures still go through degraded enumeration's
explicit error state. Debug/analyze builds passed; the final full serial
CTest rerun passed **35/35**, including `ffdoc_drift_tests` after CRLF
normalization. A fresh launch after this change produced responsive UI and
engine processes and a real `C:\` sidebar request (`ui-directory-request-
queued` -> `ui-directory-request-sent` -> `engine-directory-request-
received`) without a subsequent false overflow record in the observation
window.

`tests/engine/test_directory_watcher.cpp` now asserts that arming a watcher
produces no callback during a quiet 100 ms window before the fixture file is
created. The focused watcher test passed five consecutive runs, and the
final full CTest gate remained **35/35 PASS**.

## 19. Session-transfer snapshot (2026-08-08 continuation)

This section is the handoff for the next agent/session. It is intentionally
more operational than the earlier forensic sections. Read it after `AGENTS.md`
and before touching any source. The repository is deliberately left dirty;
that is the salvage state, not a mistake to clean up.

### 19.1 Exact repository state at transfer

* Working directory: `D:\Documents\tryPython\FastFiles`.
* Branch: `main`.
* `HEAD`: `52d33da` (`Align docs, tooling, and tests with the shipped architecture`).
* `origin/main` and `HEAD` still agree. No commit, rebase, merge, reset, restore,
  checkout, clean, stash, or cherry-pick was performed by this continuation.
* Nothing is staged. `git status --short` currently reports the long-running
  dirty salvage set: roughly 64 modified tracked files and roughly 29 untracked
  files/directories (the exact count is intentionally re-checkable, not a
  contract). The modified set includes product code, tests, OpenSpec/tooling
  synchronization, and documentation. The untracked set includes the recovery
  changes, two audit reports, EngineDiscovery, new focused tests, diagnostic
  object debris, and agent-tool directories.
* The two development binaries that this session launched were stopped by
  their exact process objects. No unrelated `WindowsTerminal` or service
  process was stopped. Do not assume a future process with the same name is
  yours; verify its absolute path before terminating it.
* The current untracked deliverables are:
  `FASTFILES_FRONTIER_HANDOFF.md` (this file), `FASTFILES_RECOVERY_REPORT.md`,
  `ENGINEERING_AUDIT_REPORT.md`, and the untracked OpenSpec changes
  `openspec/changes/recovery-degraded-browsing/` and
  `openspec/changes/address-engineering-audit-findings/`.

### 19.2 What the preceding continuation actually changed

The earlier recovery work was not a single clean commit. Treat every hunk as
salvage until a future engineer classifies it. The most important product
changes already present in the dirty tree are:

| Area | Files/symbols | Current evidence | Transfer classification |
|---|---|---|---|
| Engine discovery | `src/ui/src/EngineDiscovery.cpp/.h`, `EngineClient::LaunchEngineIfNotRunning`, `tests/ui/test_engine_discovery.cpp` | Unit/build evidence and fresh launch traces show development-tree discovery/startup can work | Preserve; re-run after any path change |
| UI-engine startup | `src/engine/src/Main.cpp`, `src/ui/src/EngineClient.cpp` | Debug/analyze builds; three fresh launches connected directly after UI-server-first ordering and bounded pipe polling | Preserve + runtime-test |
| UI directory request observability | `EngineClient::RequestDirectory`, `UiServer::HandleRequestDirectory` | Fresh diagnostics prove queued -> sent -> received for real `C:\`/`D:\` requests | Preserve; diagnostics are production state transitions, not `TEMP-DEBUG` |
| Degraded enumeration | `DegradedModeEnumerator.cpp/.h`, `UiServer.cpp` | Code and prior tests; real directory requests reached the engine | Preserve + add deeper acceptance |
| Directory watcher | `DirectoryWatcher.cpp/.h`, `tests/engine/test_directory_watcher.cpp` | Overlapped completion bug fixed; focused test repeated 5/5; full serial CTest previously 35/35 | Protected recovery work; do not replace with a simpler watcher |
| Snapshot publication | `SnapshotPublisher.cpp/.h`, `SnapshotFormat.*`, `EngineClient::ReadSnapshot` | Build/tests pass; capacity and reader-order risks remain | Preserve + later design change for torn reads/capacity |
| Column View | `ColumnView.cpp/.h`, `WindowShell.cpp` | Root/deep navigation, watcher refresh, identity-preserving selection, vertical scroll plumbing, and details geometry are covered statically; interactive DPI/long-directory/dual-pane proof remains | Preserve + finish acceptance; do not reintroduce numeric selection or raw viewport geometry |
| Storage analyzer | `StorageAnalysis.*`, `TreemapView.*`, `WindowShell.cpp`, `TreemapLayout.h` | Aggregate request plumbing, WM_NOTIFY/list routing, percentage helper, and treemap aggregate callbacks exist; degraded zero-FRN directories still cannot resolve recursive sizes | Preserve + finish vertical slice |
| Preview | `Preview.cpp` | 30 repeated preview test runs passed before the current search edits | Protected; avoid unrelated rewrites |
| Search (honest bounded slice) | `src/search/include/ffsearch/{Query.h,Search.h}`, `src/search/src/Search.cpp`, `src/ui/src/SearchPanel.*`, `tests/search/test_search.cpp` | Debug/analyze build and full serial CTest pass; broad degraded scopes are labeled honestly and capped | Preserve; extend only with a real indexed-query seam |

The older reports are evidence, not immutable truth. In particular, the early
report's “engine never launches from the build tree” observation is superseded
by the later EngineDiscovery/startup work and fresh request diagnostics. The
placeholder signature pins remain current and are not superseded.

### 19.3 The active WIP slice: honest, bounded indexed-search behavior

The continuation was beginning the next capability after degraded browsing:
make search truthful and keep candidate construction off the UI thread, using
the existing snapshot seam rather than inventing a new IPC command. The exact
unverified edits are:

1. `ffsearch::Candidate` in `src/search/include/ffsearch/Query.h:14-28` gained
   `bool persistentIndexed = true`. The default intentionally preserves old
   headless/library aggregate initializers; the UI marks degraded-only records
   false at its snapshot boundary.
2. `ffsearch::SearchRequest` in `src/search/include/ffsearch/Search.h:17-34`
   gained `maxResults` (zero means unlimited for non-UI callers) and
   `persistentOnly`. `SearchResponse` gained `truncated`.
3. `src/search/src/Search.cpp:22-100` filters candidates when
   `persistentOnly` is set and stops after `maxResults`, reporting truncation.
4. `SearchPanel::Dispatch` (`src/ui/src/SearchPanel.cpp:331-361`) sets a UI cap
   of `kMaxUiSearchResults = 10000` and enables `persistentOnly` only for broad
   scopes when at least one snapshot directory has a non-zero
   `volumeRowId` (the marker used by the existing persistent projection).
5. `SearchPanel::WorkerMain` (`SearchPanel.cpp:363-384`) now materializes the
   snapshot-to-candidate vector on the worker thread, checks generation before
   executing, and posts only the bounded result set back to the UI.
6. `SearchPanel::OnSnapshotUpdated` (`SearchPanel.cpp:753-775`) invalidates a
   shared immutable candidate cache and dynamically detects persistent coverage.
   `RefreshScopeLabels` (`SearchPanel.cpp:777-796`) labels the four scopes as
   either indexed coverage or browsed-only coverage. Broad degraded labels are
   now explicitly “Browsed Folders Only” / “All Browsed Locations”.
7. `SearchPanel::PopulateCandidates` (`SearchPanel.cpp:798-866`) caches one
   worker-built vector per snapshot epoch and tags directory/entry candidates
   by `volumeRowId`. This is intended to stop repeated full snapshot parsing on
   every keystroke, but the cache/generation behavior still needs review.
8. `tests/search/test_search.cpp:39-49` adds persistent-only exclusion and
   bounded-result assertions. The existing test uses aggregate initialization;
   verify field order and make the exclusion assertion identity-based if it is
   fragile under sorting.

This slice is a deliberate bounded step, not proof of a global NTFS index.
The service is still fail-closed behind all-zero signature pins, so a live
snapshot normally contains degraded/browsed data only. When persistent rows
do appear, broad scopes restrict themselves to those rows; current-folder
scopes intentionally remain useful against available snapshot data. The
existing architecture still has no UI search IPC request, by design.

### 19.4 WIP validation status and likely review points

Before these newest search edits, the following gates were green: Debug build,
`/analyze` build, serial CTest **35/35**, focused watcher test 5/5, and the
preview test repeated 30/30. **There is no post-search-patch build/test result
yet.** Do not report “35/35 after search” until the next session reruns it.

Review these points before any further implementation:

* Check all `Candidate` aggregate initializers in `src/` and `tests/` for the
  appended boolean. The default protects most callers, but UI initializers in
  `SearchPanel.cpp` are positional and must remain correct.
* Check that `SearchPanel::OnSnapshotUpdated` is actually called on every
  `WM_APP_SNAPSHOT_UPDATED` path. Current `WindowShell.cpp:1193-1197` calls
  ColumnView, SearchPanel, and StorageAnalysis in that message handler.
* Check that scope labels and `SetDefaultScope` semantic names remain stable
  for persisted settings. The dynamic labels are explanatory text, not setting
  keys; do not serialize the display strings.
* Check the cache race: `candidateCacheEpoch_` invalidation, worker materialize,
  generation cancellation, and `candidateCache_` lifetime must not allow a
  stale vector to be retained as current. A stale vector may be used for one
  cancelled query, but must not be published for a newer generation.
* Check the result cap semantics: the loop currently marks `truncated` as soon
  as the cap is reached, even if the cap was exactly the total matching count.
  If truthful “more results exist” semantics are required, inspect one extra
  match or document the simpler bounded-work meaning.
* Check the all-index scope mapping. `SearchScope` is ordinal; using an ordinal
  comparison for “broad” scopes is only correct if the enum remains ordered as
  `CurrentFolder`, `CurrentFolderAndSubfolders`, `CurrentDrive`,
  `AllIndexedLocations`.
* Check high FRN fields. `SnapshotFormat` carries high/low IDs, while
  `ffsearch::Candidate` and `SearchPanel` currently retain only low IDs. This
  is a future correctness/scalability gap, not a reason to weaken the service
  boundary.

### 19.5 Exact next-session procedure

1. Read `AGENTS.md`, this handoff, and the current `git diff` for the symbols in
   §19.3. Do not run an OpenSpec command that auto-refreshes generated agent
   tooling and then count mtime-only changes as product work.
2. Re-run `git status --short --branch`, `git diff --cached`, and
   `git diff --stat`. Confirm the tree is still unstaged. Never use a reset or
   restore to “separate” changes; use read-only diff inspection and manual
   `apply_patch` only if a user explicitly authorizes implementation.
3. Configure/build only from a Visual Studio developer PowerShell. Set
   `FASTFILES_NINJA_EXE` to the VS-bundled Ninja first. Stop only exact
   development-tree FastFiles processes if they are left over from a smoke.
4. Run the focused search test (`ffsearch_tests`) and then serial CTest. Run
   Debug and `/analyze` builds. If a test fails, capture the failure and update
   this handoff; do not weaken the test or mark an OpenSpec checkbox.
5. If the user has exited Explore mode and authorizes implementation, run a
   controlled runtime smoke: launch the development UI, verify the sibling
   engine path, wait for `engine-connect-succeeded`, post a real root click,
   and inspect only the normal diagnostics log. Confirm
   `ui-directory-request-queued` -> `ui-directory-request-sent` ->
   `engine-directory-request-received` -> snapshot refresh. Do not read
   `verify/runs`, `verify/baselines`, or `verify/.signing`.
6. Update this file with exact commands/results before starting another slice.
   Leave the source tree and all untracked diagnostics intact for the next
   agent. Do not edit recovery task checkboxes as bookkeeping.

### 19.6 Suggested acceptance for the search slice

The next agent should not call this slice complete until all of the following
are separately evidenced:

* `ffsearch_tests` passes with ordinary, persistent-only, and bounded-result
  cases; cancellation behavior remains passing.
* Debug and `/analyze` builds are warning-clean.
* Full serial CTest passes and the doc-drift test remains green.
* A snapshot with only degraded directories displays browsed-only scope labels
  and an all-browsed query can find a known item in a browsed fixture.
* A synthetic/persisted-marked snapshot displays indexed labels and a broad
  persistent-only query excludes a deliberately degraded candidate.
* Repeated keystrokes do not block the UI thread (static/thread evidence is
  acceptable if a deterministic UI timing harness is unavailable).
* The 10,000-result cap is visible in status text and no unbounded vector is
  returned to the list control.
* No claim is made that this activates MFT/USN indexing; pins remain unchanged.

### 19.7 Protected invariants for the next agent

Do not alter the closed service command enum, connection-scoped opaque handles,
frame-size/count validation, rejected length-prefixed records, symmetric
image-path plus pinned-signature authentication, query-only client SCM rights,
`FILE_FLAG_FIRST_PIPE_INSTANCE`, MFT `$DATA` exclusion, install-directory ACL
hardening, or `SetDefaultDllDirectories` behavior. Do not populate the
all-zero pins, fabricate certificates, bypass service authentication, add a
generic privileged path/handle command, or make the UI run elevated. Those are
architectural/security decisions, not temporary test blockers.

### 19.8 After search: bounded recovery order

Once the search slice has evidence, the shortest technically sound sequence is:

1. Formal degraded-browsing acceptance: five-level navigation, Back/Forward/
   Up, watcher-created-file refresh, engine failure visibility, and a stable
   fixture/UI seam. The request path is now proven once, not end-to-end proven.
2. Storage vertical: route `WM_NOTIFY` to `StorageAnalysis`, populate owner-data
   lists, and retain the existing generation-guarded aggregate callbacks. Keep
   degraded directories explicitly partial rather than inventing sizes.
3. Navigation state funnel: establish one canonical current path/selection/
   history owner, then repair tab restore, dual-pane command routing, scrolling,
   DPI hit testing, and details-panel geometry.
4. Snapshot correctness/scalability: reader generation retry, publish failure
   surfacing, capacity strategy, and per-directory export instead of whole-index
   republish per ingestion batch.
5. Product decision for the privileged path: provision real signed pins with a
   controlled release process, or explicitly scope an unprivileged full-tree
   indexer. Do not silently choose either by weakening authentication.
6. Only then address shell parity (system clipboard, Properties, context verbs),
   performance/memory ceilings, long-path manifest/platform coverage, and
   release/install verification.

### 19.9 Transfer command block

The next session can paste this as its opening brief:

```text
You are continuing FastFiles recovery in D:\Documents\tryPython\FastFiles.
Read AGENTS.md and FASTFILES_FRONTIER_HANDOFF.md first. Preserve the dirty
working tree; never reset/restore/checkout/clean/stash/revert/rebase/merge/
cherry-pick/commit. HEAD is 52d33da on main, nothing staged. The newest WIP
slice adds truthful persistent-only search, a 10,000-result cap, and worker-
thread snapshot candidate caching in Query.h/Search.h/Search.cpp/SearchPanel.*
and tests/search/test_search.cpp. It has NOT been built/tested after that
patch. First inspect it, then run the focused search test, Debug/analyze builds,
and serial CTest with the VS Ninja preset. Do not alter signature pins or any
security invariant. Earlier verified gates before this WIP were Debug + analyze
builds, serial CTest 35/35, watcher 5/5, preview 30/30, and fresh runtime
queued->sent->received directory requests. Update the handoff with evidence
before taking the next bounded slice.
```

End of transfer snapshot. The correct next action is validation and evidence,
not another broad rewrite.

## 20. Search-slice validation and correction (2026-08-08 continuation)

The next session implemented and validated the WIP search behavior described in
§19, then corrected two semantic races before accepting it.

### 20.1 Corrections made

* `src/ui/src/SearchPanel.cpp:351-357` now sets `persistentOnly` for both
  `Current Folder + Indexed Subfolders` and the two broader scopes whenever the
  snapshot advertises persistent coverage. `Current Folder — Available Data`
  remains the deliberate mixed/degraded fallback. This makes the dynamic labels
  truthful instead of letting scope 1 include degraded-only records.
* `SearchPanel::OnSnapshotUpdated` (`SearchPanel.cpp:753-787`) now increments
  the UI generation before invalidating the candidate cache. A worker using the
  old snapshot can no longer publish a result after the new mapping is visible.
  If the search panel is visible and the query is non-empty, it dispatches a
  replacement search without recording a duplicate history entry.
* `SearchPanel::Dispatch(bool recordHistory = true)` and its declaration in
  `SearchPanel.h` carry the refresh distinction. Ordinary keystrokes still
  record history; snapshot-driven refreshes do not.
* `tests/search/test_search.cpp:39-50` now exercises persistent-only
  `CurrentFolderAndSubfolders` as well as all-location filtering. The expected
  count is three persistent records in the fixture; the degraded D: record is
  excluded. The test wording was corrected to describe records, not only
  descendants.

### 20.2 Validation evidence after the corrections

* Initial post-WIP Debug build: PASS.
* Initial post-WIP `/analyze` build after reconfiguring the stale analyze cache
  to the Community VS toolchain: PASS.
* Focused `ffsearch_tests` + `ffsearch_scan_tests`: PASS after correction.
* Final Debug build: PASS.
* Final `/analyze` build: PASS.
* Final serial CTest: **35/35 PASS**, including protocol fuzz tests, file-op
  E2E, watcher, snapshot, UI helper, PowerShell-gated tests, and doc drift.
* One intermediate full CTest failure was only the new test's incorrect expected
  count (2 instead of 3); it was corrected and the rerun passed. No test was
  weakened.
* The first CTest after the WIP patch found mixed line endings in six touched
  files. Those files were normalized to CRLF, then the full suite passed.

### 20.3 Runtime evidence after the corrections

A fresh development-tree launch of the rebuilt binary produced:

```text
engine-launched
engine-connect-succeeded
ui-directory-request-queued path=C:\
ui-directory-request-sent path=C:\
engine-directory-request-received path=C:\
ui-snapshot-column-refresh path=C:\ error=0x0 items=35
```

The UI and sibling engine both reported responsive, and both were stopped by
their exact absolute-path process objects afterward. This is runtime evidence
for startup, discovery, pipe connection, directory request, degraded listing,
and snapshot refresh. It is not evidence that the privileged service is active,
that five-level UIA navigation passes, or that a real persistent index exists.

### 20.4 Current gate command note

The first build attempt in a plain PowerShell failed because `INCLUDE`/SDK
variables were absent (`windows.h` and `cstdint` not found). The repository's
prescribed VS environment import fixed this. The `build/analyze` cache had also
pointed at a separate Visual Studio 18 BuildTools installation; re-running the
analyze configure preset under Community VS fixed that cache. A future agent
should use the following pattern rather than treating the missing-header error
as a source failure:

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$dump = cmd.exe /c "call `"$vs`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $dump) {
    if ($line -match '^(.*?)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:FASTFILES_NINJA_EXE = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build --preset debug
& $cmake --build --preset analyze
```

### 20.5 What remains unresolved after this slice

The search panel is now bounded, off-UI-thread, generation-aware, and honest
about degraded versus persistent coverage, but it is still a snapshot scan.
The service remains fail-closed behind placeholder pins, broad “All Indexed”
coverage has no live MFT/USN data, high FRN fields are not yet carried through
`ffsearch::Candidate`, and the 8 MiB snapshot architecture remains a scale
ceiling. The next engineering slice should therefore be a user-facing
degraded-browse/storage acceptance seam, not a security bypass or a claim of
production indexing.

## 21. Shell interoperability slice (2026-08-08 continuation)

Two small but material daily-use gaps were closed after the search gate:

* `src/ui/src/QuickActions.h/.cpp` now provides
  `CopyFilesToClipboard`, `ReadFilesFromClipboard`, and
  `HasFilesOnClipboard`. FastFiles publishes standard `CF_HDROP` data plus the
  shell `Preferred DropEffect` format (`DROPEFFECT_COPY`/`DROPEFFECT_MOVE`),
  validates the serialized allocation before opening/emptying the clipboard,
  and reads external Explorer-style file lists without retaining clipboard
  memory. The app-local vector remains as a fallback when the clipboard is
  temporarily unavailable.
* `src/ui/src/WindowShell.cpp::CurrentCommandContext` treats a shell file list
  as paste-compatible even when no internal copy was made. The `file.copy` and
  `file.cut` handlers publish shell data; `file.paste` consumes either the
  internal fallback or an external `CF_HDROP` list and preserves move/copy
  semantics. The exact source list is cleared only for an internal cut after a
  successfully queued move.
* The `item.properties` handler now invokes the native
  `SHObjectProperties(..., SHOP_FILEPATH, ...)` shell property sheet and shows
  an explicit error if Windows cannot open it, instead of only changing the
  FastFiles title bar.

Validation after this slice:

* Debug build: PASS.
* `/analyze` build: PASS.
* `ffcommand_tests`, `fffileoperations_tests`, and
  `fffileoperations_e2e_tests`: PASS.
* Final serial CTest: **35/35 PASS**, including doc-drift.

Clipboard behavior has compile/test coverage through the command target but no
automated cross-process Explorer clipboard fixture yet. A future interactive
acceptance should copy from Explorer into FastFiles, copy/cut from FastFiles
into Explorer, verify the move arrow semantics, and confirm paste remains
disabled when the clipboard contains text only.

## 22. Fresh runtime input smoke and transfer point (2026-08-08 continuation)

The rebuilt development UI was launched from the exact absolute path
`build/debug/src/ui/FastFiles.exe`; it spawned the sibling
`build/debug/src/engine/FastFilesEngine.exe`. Both processes were responsive.
The normal per-user log is `%LOCALAPPDATA%\\FastFiles\\logs\\diagnostics.log`
(not a repository-local file). A controlled Win32 `WM_LBUTTONDOWN`/`UP` was
posted to the live window after startup. The click was not synthetic-only at
the model layer: it caused a real degraded directory request and snapshot
refresh for a child of `C:\\`:

```text
engine-launched path=D:\\Documents\\tryPython\\FastFiles\\build\\debug\\src\\engine\\FastFilesEngine.exe
engine-connect-succeeded
ui-directory-request-queued path=C:\\$Recycle.Bin
ui-directory-request-sent path=C:\\$Recycle.Bin
engine-directory-request-received path=C:\\$Recycle.Bin
ui-snapshot-column-refresh path=C:\\ items=35 error=0x0
ui-snapshot-column-refresh path=C:\\$Recycle.Bin items=2 error=0x0
```

This is **CONFIRMED runtime evidence** for startup, deterministic engine
discovery/launch, UI-pipe connection, a genuine WM input-to-column activation,
request serialization, engine dispatch, degraded enumeration, and snapshot
publication/readback. It is not evidence for five-level navigation, watcher
refresh, UIA acceptance, Explorer clipboard interoperability, a live indexed
dataset, or the privileged service. Exact-path process objects were stopped
after the smoke; no FastFiles or FastFilesEngine process should remain.

The successful click matters because the old investigation had only observed
connection and subscribe. The remaining folder-navigation proof gap is now
narrower: exercise repeated column transitions (ideally against a dedicated
temporary nested fixture), Back/Forward/Up, and a watcher-created file while
capturing the same normal diagnostics log. Do not read `verify/runs`,
`verify/baselines`, or `verify/.signing` to obtain that evidence.

## 23. Current exact dirty-tree transfer inventory

The tree remains intentionally dirty and unsaved. Nothing is staged. HEAD is
still `52d33da` on `main`. Product WIP includes the engine degraded path,
snapshot/watcher hardening, UI navigation/storage/search changes, shell
clipboard/Properties interop, and their tests. Tooling-sync edits under
`.agent/`, `.gemini/`, `.opencode/`, `.claude/`, `.cline/`, `.codex/`,
`.cursor/`, `.kimi/`, plus `AGENTS.md`, `.mcp.json`, and `progress.md` must be
kept separate from product decisions. Preserve the untracked audit/report
files, recovery OpenSpec change, unsafe `address-engineering-audit-findings`
draft, EngineDiscovery sources/tests, helper headers/tests, and diagnostic
`.obj` debris. Do not delete or stage anything during handoff.

The current status summary observed at this transfer point is approximately 66
modified tracked paths and 29 untracked paths. Always re-run `git status
--short` before acting; the exact count can change because OpenSpec tooling may
refresh generated integration-file mtimes without changing content.

## 24. Correct next-session opening brief

Paste this into the next agent/session:

```text
Continue FastFiles from D:\\Documents\\tryPython\\FastFiles. Read AGENTS.md
and FASTFILES_FRONTIER_HANDOFF.md completely. Preserve the dirty salvage tree;
never reset/restore/checkout/clean/stash/revert/rebase/merge/cherry-pick/
commit, and do not delete untracked files. Do not read verify/runs,
verify/baselines, or verify/.signing. Do not edit OpenSpec checkboxes.

State: main at HEAD 52d33da, nothing staged. The latest validated product WIP
adds truthful persistent-only search plus worker-thread candidate caching and
a 10,000-result cap; shell-standard CF_HDROP copy/cut/paste interop; native
SHObjectProperties; and earlier degraded-engine/request/snapshot/watcher,
column-view, storage, input, preview, and tab fixes. Debug and /analyze builds
and serial CTest 35/35 passed after those edits. The privileged MFT/USN path
remains intentionally inactive because PinnedSignatures.h contains all-zero
placeholder pins. Never bypass or populate them.

Fresh runtime evidence: exact development-tree FastFiles.exe launched its
sibling FastFilesEngine.exe, connected, accepted a real posted WM click, and
logged queued -> sent -> engine-directory-request-received -> snapshot refresh
for C:\\$Recycle.Bin (C:\\ had 35 entries). Processes were stopped by exact
absolute executable path afterward. This proves one real input/request seam,
not full five-level acceptance.

Before the next code slice: inspect current status/diff, import the Community
VS developer environment, run a focused test and the full serial gate if the
tree changed, then choose one bounded usability seam. Highest-value next seam
is formal degraded browsing acceptance using a temporary nested fixture:
address-bar navigation, repeated folder activation/keyboard Enter, Back,
Forward, Up, and watcher-created-file refresh. Capture only the normal
%LOCALAPPDATA%\\FastFiles\\logs\\diagnostics.log. If implementation is needed,
keep it small and add a meaningful test; do not rewrite architecture.

Update this handoff after every material change with exact paths/symbols,
build/test/runtime evidence, unresolved risks, and the next exact slice.
```

## 25. Double-click file opening slice (2026-08-08 continuation)

The next bounded daily-use gap was confirmed in code: the main window class
registered only `CS_HREDRAW | CS_VREDRAW`, so it did not request native
`WM_LBUTTONDBLCLK` messages, and `WindowShell::HandleMessage` had no file
double-click branch. `item.open` existed and worked when invoked through the
command surface, but ordinary double-clicking a file could not reach it.

The slice is now implemented without changing folder semantics:

* `src/ui/src/WindowShell.cpp:556` adds `CS_DBLCLKS` to the registered main
  window class.
* `src/ui/src/ColumnView.h:236-247` adds the pure policy seam
  `ShouldOpenSelectionOnDoubleClick(std::span<const SelectionItem>)`.
  It returns true only for exactly one non-directory selection. A folder is
  already activated by Column View's first click and must not be activated
  twice against the newly focused column; multi-selection is also excluded.
* `src/ui/src/WindowShell.cpp:1504-1516` handles `WM_LBUTTONDBLCLK`. The first
  click has already selected/navigated; a single selected file is routed to
  the existing `InvokeCommand(L"item.open")`. Drag arming is cleared.
* `tests/ui/test_column_hit_test.cpp:51-59` covers file, folder, and
  multi-selection policy cases through the pure seam.

Validation:

* Focused Debug target build (`ffcolumn_hit_test_tests`, `FastFiles`): PASS.
* Focused `ffcolumn_hit_test_tests`: PASS.
* `/analyze` build: PASS.
* First full CTest run: 34/35 product tests passed; only `ffdoc_drift_tests`
  rejected mixed line endings in the three touched files. The files were
  normalized to CRLF (no source-content change), then the complete serial
  CTest rerun passed **35/35**, including fuzz, file-operation E2E, watcher,
  snapshot, UI helper, PowerShell-gated, security-descriptor, and doc-drift
  tests.
* Fresh runtime class inspection after launching the rebuilt exact development
  binary found `FastFilesMainWindow` with style `0xB`; `CS_DBLCLKS` (`0x8`) was
  present. UI and engine processes were stopped by exact executable path.

This slice is compile/test/runtime-style verified, but a real file-open smoke
against a safe temporary `.txt` association remains optional because it would
launch an external editor. The existing `item.open` command path is already
covered by command/file-operation tests and remains unchanged.

## 26. Current next slice after double-click

The degraded browser now has direct evidence for five nested folder clicks and
the main window can receive native double-click messages. The next highest
value is watcher acceptance: create a file in the dedicated temporary fixture
while its directory is visible, wait for a new snapshot generation, and prove
the row appears without manual refresh. If that passes, exercise Back,
Forward, and Up from the same fixture. Only if a defect appears should source
changes be made; do not invent a larger navigation refactor without a failing
edge. Keep using `%LOCALAPPDATA%\\FastFiles\\logs\\diagnostics.log` for runtime
evidence and never inspect the forbidden verification artifact directories.

## 27. Watcher and Up runtime acceptance (2026-08-08 continuation)

The dedicated fixture was driven to
`C:\\Users\\palac\\AppData\\Local\\Temp\\FastFiles-nested-smoke\\L1\\L2\\L3\\L4\\L5`
through the real address edit. Before the external file creation, the normal
log showed a successful request and `ui-snapshot-column-refresh ... items=1`
(the fixture already contained a prior smoke file). A new uniquely named
`watch-created-*.txt` was then written in that exact visible directory. Four
seconds later the log showed a second refresh for the same path with
`items=2`, with no manual refresh command:

```text
ui-directory-request-sent path=...\\L5
engine-directory-request-received path=...\\L5
ui-snapshot-column-refresh path=...\\L5 items=1 error=0x0
ui-snapshot-column-refresh path=...\\L5 items=2 error=0x0
```

This is **CONFIRMED runtime evidence** for a browsed-root watcher noticing a
new file, re-enumerating, republishing, notifying the UI, and refreshing the
visible listing. The earlier run that wrote into L5 while only the fixture root
was visible is intentionally not counted as watcher evidence; it correctly
produced no L5 refresh and demonstrated the test was scoped to watched roots.

The same deep address-bar launch was followed by a posted `VK_BACK` message to
the main window. The active column moved from L5 to L4 and the log showed a new
L4 request/refresh (`items=1`). This confirms the Backspace/Up shortcut reaches
the existing `navigation.up` command and rehydrates the requested parent. The
fixture run and engine/UI processes were stopped by exact executable path.

Remaining runtime acceptance gaps are now narrower: exercise the actual Back
and Forward chrome buttons (history across multiple folder clicks), perform a
safe file double-click against a controlled association or a non-launching
test seam, and verify engine-loss/status-badge behavior. None of these justify
weakening service authentication or changing the privileged path.

## 28. Synthetic This-PC startup alignment (2026-08-08 continuation)

Static inspection found a genuine startup-state mismatch: `ColumnView::Initialize`
starts with a synthetic drive-root column whose path is the empty sentinel,
while `WindowShell` previously initialized `NavigationWorkspace` to `C:\\`.
The default persisted setting is the logical `This PC` location, and the first
screen therefore could show the drive-root model while breadcrumbs/tab state
claimed a real `C:\\` path. The fix keeps the logical-root state explicit and
does not alter persisted real-path restoration:

* `src/ui/src/WindowShell.h` now defaults
  `navigationWorkspace_` to `NavigationWorkspace{L""}`; the comment documents
  the empty synthetic `This PC` sentinel and that configured/restored paths
  replace it during initialization.
* `src/ui/src/NavigationChrome.cpp` labels an empty tab path `1: This PC` while
  retaining the existing leaf-name labels for real paths.
* `tests/navigation/test_navigation_workspace.cpp` adds
  `TestSyntheticRootContext`, asserting the empty path and one non-clickable
  `This PC` breadcrumb segment.

Validation:

* Focused Debug build of `ffnavigation_tests` and `FastFiles`: PASS.
* Focused `ffnavigation_tests` and `ffdoc_drift_tests`: PASS.
* Fresh isolated runtime launch with a temporary `LOCALAPPDATA` (so no prior
  workspace state could mask the default) found the navigation chrome tab text
  `1: This PC` in the rebuilt exact development binary. Processes were stopped
  by absolute executable path.
* Re-run `/analyze` build: `ninja: no work to do` / exit 0.
* Full serial CTest after this slice: **35/35 PASS**.

The remaining concern is semantic rather than a current failure: typed
`This PC` in the address edit is still not a filesystem path accepted by the
existing parser. Monitor that path during later address-bar work; do not add a
new logical-root grammar unless a runtime acceptance test demonstrates the
need.

## 29. Logical-root address entry slice (2026-08-08 continuation)

The monitor concern above became a small, directly testable usability defect:
the synthetic root's address edit was blank, and committing the visible logical
label `This PC` was rejected as a relative path. The existing root model already
uses an empty path sentinel, so the fix teaches the address seam that one label
to round-trip to that sentinel:

* `src/ui/src/NavigationWorkspace.cpp:179-184` now presents `This PC` while
  editing an empty current path.
* `src/ui/src/NavigationWorkspace.cpp:416-417` accepts case-insensitive
  `This PC` after trimming/quote removal and returns the empty logical path.
* `src/ui/src/NavigationWorkspace.cpp:442-446` commits that empty parsed path
  through `workspace.Navigate({})` without calling `GetFileAttributesW` on an
  empty string.
* `tests/navigation/test_navigation_workspace.cpp:22-24,31-40` covers parse,
  edit-display, and round-trip commit back from `C:\\` to the synthetic root.

Validation after the slice:

* Focused Debug `ffnavigation_tests` and `FastFiles` build: PASS.
* Focused navigation + doc-drift tests: PASS after normalizing the two touched
  source/test files to CRLF, as required by the repository drift guard.
* `/analyze` build: PASS (three objects relinked cleanly).
* Full serial CTest: **35/35 PASS**.

No privileged code, protocol, installer, or authentication behavior changed.
The only remaining question is an optional direct GUI proof of entering `This
PC`; the pure parser/workspace test now exercises the exact state transition.

## 30. Synthetic-root UI integration completion (2026-08-08 continuation)

Static review of the preceding logical-root slice found one integration hole:
`CommitAddressBarPath` could correctly move `NavigationWorkspace` to the empty
sentinel, but `NavigationChrome::NavigateTo` suppressed empty callbacks and
`ColumnView::NavigateToPath` intentionally rejected empty strings. The UI could
therefore claim `This PC` while still rendering the last real directory.

The bounded integration fix is:

* `src/ui/src/ColumnView.h` declares `NavigateToComputerRoot()` and private
  `MakeComputerRootColumn()`.
* `src/ui/src/ColumnView.cpp` centralizes the existing logical-drive filtering
  in `MakeComputerRootColumn()`, reuses it from `Initialize`, and implements
  `NavigateToComputerRoot()` to clear only the active pane, reset focus/vertical
  scroll/pending selection/hover, and repopulate the synthetic drive rows
  without issuing an engine request.
* `src/ui/src/NavigationChrome.cpp` now invokes its navigation callback even for
  an empty path.
* `src/ui/src/WindowShell.cpp` routes an empty chrome path to
  `columnView_.NavigateToComputerRoot()` and real paths to the existing
  `NavigateToPath` path.

Validation:

* Debug build of `FastFiles` and `ffnavigation_tests`: PASS.
* Focused navigation + doc-drift tests: PASS after CRLF normalization of the
  four touched UI files.
* `/analyze` build: PASS (UI objects and column hit-test test rebuilt cleanly).
* Full serial CTest: **35/35 PASS**.

This closes the logical-root state/render mismatch while preserving dual-pane
state and the security-critical engine boundary. A direct GUI address-entry
smoke remains optional; the callback and reset path are now statically and
compile/test verified.

## 31. Post-integration runtime/build handoff (2026-08-08 continuation)

After the synthetic-root integration, the rebuilt development binary was
launched from the exact sibling layout (`build/debug/src/ui/FastFiles.exe` with
`build/debug/src/engine/FastFilesEngine.exe`). The normal diagnostics log again
recorded `engine-launched`, `engine-connect-succeeded`, queued directory
requests, `engine-directory-request-received`, and snapshot refreshes. The
processes were stopped by exact absolute executable path after the smoke; no
FastFiles process remains running.

Final gates for the current tree are green:

* `/analyze` build: PASS (the synthetic-root UI objects and UI test relinked).
* Full serial CTest: **35/35 PASS**, including protocol fuzzing, file-operation
  E2E, watcher/snapshot tests, navigation, UI helpers, PowerShell harness
  tests, security-descriptor tests, and doc-drift.
* `git diff --check`: only the repository's pre-existing LF/CRLF warnings for
  tooling-sync files; no whitespace error in the product slices.

The next session should not redo these root-contract changes. Remaining
runtime gaps are optional direct address-entry proof, chrome Back/Forward
button proof, engine-loss/status-badge proof, and real file-open association
proof. The highest-risk product limitation remains the intentionally inactive
privileged index path caused by zero signature pins; degraded browsing is the
validated daily-use path.

## 32. Engine-loss runtime acceptance (2026-08-08 continuation)

The exact development UI was launched, allowed to start/connect its sibling
engine, and then only that exact engine executable was terminated. The normal
diagnostics log recorded the failure/recovery sequence:

```text
engine-launched
engine-connect-succeeded
engine-disconnected
engine-launched
engine-connect-succeeded
```

The UI remained alive while the engine was replaced, and both exact processes
were stopped afterward. This is **CONFIRMED runtime evidence** for the
engine-loss reconnect path. It does not by itself prove visual inspection of
the transient `Starting`/`Unavailable` badge; the engine recovered within the
bounded cycle, so the terminal unavailable state was not reached in this
smoke. The status callback and retry affordance remain statically wired at
`WindowShell::Initialize` and `ColumnView::OnMouseDown`.

## 33. History/tab synthetic-root consistency (2026-08-08 continuation)

The logical-root path was also reachable through command-driven history, not
only the address edit. `navigation.back`, `navigation.forward`, new/closed/
reopened tabs, and tab switching previously called `ColumnView::NavigateToPath`
directly; its deliberate empty-path guard would leave a real directory rendered
after the workspace returned to `This PC`.

`src/ui/src/WindowShell.h` now declares `NavigateColumnViewTo`, and
`src/ui/src/WindowShell.cpp:139-142,315-366,626-628` routes all of those
workspace-derived paths through it. Empty paths call
`NavigateToComputerRoot`; real paths retain `NavigateToPath`. The chrome
callback uses the same helper, so address, history, and tab transitions share
one state/render contract.

Validation:

* Debug `FastFiles` + navigation test build: PASS.
* Focused navigation/doc-drift tests: PASS after CRLF normalization.
* `/analyze` build: PASS.
* Full serial CTest: **35/35 PASS**.

No protocol, service, or security behavior changed. A future runtime smoke
should exercise Back from a real path to the synthetic root once, but the
static call-site audit now covers every command path that can produce an empty
workspace location.

## 34. Command-palette startup overlay fix (2026-08-08 continuation)

Fresh UIA/Win32 inspection found that the command palette's hidden controls were
visible on a new window. `WindowShell` calls `commandPalette_.Reposition()` on
the initial `WM_SIZE`; `CommandPalette::Reposition()` used `SWP_SHOWWINDOW`
unconditionally even though `visible_` was still false. The palette edit and
list were therefore direct visible children over the browser, an input/focus
hazard independent of the palette's logical `Visible()` state.

`src/ui/src/CommandPalette.cpp:91-97` now returns immediately while the palette
is closed. `Show()` sets `visible_ = true` before calling `Reposition()`, so the
open path still positions and shows both controls; `Hide()` still explicitly
hides them. Search and storage panels already guard their own `Reposition()`
methods with `visible_` and were not changed.

Runtime evidence on a fresh rebuilt process:

```text
FastFilesNavigationChrome visible=True
Edit visible=False
Edit visible=False
ListBox visible=False
Edit visible=False
```

The navigation chrome remained visible while all direct palette/search/settings
edit/list controls were hidden. The exact UI/engine processes were stopped after
the probe.

Validation:

* Debug `FastFiles` build: PASS.
* `/analyze` build: PASS.
* Full serial CTest: **35/35 PASS**.
* No security, protocol, or privileged-path changes.

## 35. Shortcut smoke result and evidence boundary (2026-08-08 continuation)

After the overlay fix, a fresh development binary was exercised with the
repository's UIA driver. `Send-UiaInput` successfully reported the expected
`{CTRL}L`, `{CTRL}F`, and `{CTRL}{SHIFT}P` key sequences, but the UIA tree did
not expose the address/search/palette edit controls afterward. This is not yet
classified as a product shortcut failure: the driver reported its fallback
input path, and the same run could not invoke a button through the available
driver command. The earlier direct Win32 child-window probe remains the
authoritative result for the startup overlay (all hidden controls were
actually `IsWindowVisible == false`).

Static shortcut wiring is intact:

* `WindowShell::RunMessageLoop` checks global shortcuts before active-view
  shortcuts and deliberately exempts native `Edit` controls.
* `WindowShell::DispatchShortcut` resolves the current modifier state through
  `ShortcutMap::FindByChord` and invokes the registered command.
* `CommandSystem.cpp` registers `navigation.focus-path` (`Ctrl+L`),
  `search.focus` (`Ctrl+F`), and `app.command-palette` (`Ctrl+Shift+P`) as
  global bindings applicable to an empty selection.

Do not change shortcut dispatch solely from the inconclusive UIA smoke. A
future session should either capture a real foreground interactive-desktop
proof or add a narrow message/command seam test before touching this path.
The exact UI and sibling engine processes were stopped after the smoke.

## 36. Mouse tab-state preservation slice (2026-08-08 continuation)

Static call-site tracing found a real tab usability defect: keyboard
next/previous-tab handling already saved and restored ColumnView state, but
mouse tab buttons, new-tab, and close-tab transitions only changed
`NavigationWorkspace::ActiveContext().currentPath`. They discarded the target
tab's materialized column chain, focus, and scroll state, so a tab could switch
to the right address while rendering the previous tab's columns or rebuilding
only the leaf path.

The bounded fix is now in the dirty working tree:

* `src/ui/src/NavigationChrome.h:12-23,42,62-64` adds a two-argument navigate
  callback (`path`, `restoreTabState`) and a `BeforeTabChangeHandler`.
* `src/ui/src/NavigationChrome.cpp:67-72,175-178,250-258,556-568` invokes the
  before-change callback before mouse tab/new/close transitions and requests
  state restoration only for an actual tab transition. Ordinary address,
  breadcrumb, drive, and history navigation remain fresh path navigation.
  Inactive tab buttons remain enabled (the previous disabled state made mouse
  switching impossible).
* `src/ui/src/WindowShell.h:68-69` and
  `src/ui/src/WindowShell.cpp:144-174,656-663` save the active pane's column
  paths, focused column, and horizontal scroll into the active
  `NavigationContext`; tab transitions restore those values through
  `ColumnView::RestoreActivePaneState`. The synthetic empty `This PC` root is
  restored through `NavigateToComputerRoot()` instead of issuing an empty
  engine request.
* Command-driven new/close-tab and sidebar open-in-new-tab paths also call
  `SaveActiveTabViewState` (`WindowShell.cpp:342-357,657-674`).

Evidence and limits:

* Debug `FastFiles` and `ffnavigation_tests` rebuilt successfully after the
  callback/API change.
* Full serial CTest passed **35/35**, including `ffnavigation_tests` and the
  UI helper tests; `/analyze` rebuilt `FastFiles` successfully.
* Static inspection confirms both mouse and keyboard transition paths now
  use the same save/restore contract. This slice preserves the active pane
  only; dual-pane per-tab state is still incomplete because
  `NavigationContext`/`ColumnView` do not yet expose a second-pane snapshot.
* A scoped Win32 probe launched the exact development UI and found the real
  chrome/tab controls, but its cross-thread address-edit injection did not
  commit `C:\Windows`/`C:\Users` (labels remained `1: This PC`, `2: This PC`).
  Therefore this run is **NOT runtime proof** of tab restoration; it is a
  harness boundary, not a source verdict. No files were modified and exact
  UI/engine processes were stopped afterward.

Next agent must not mark tab restoration runtime-verified until a reliable
foreground/UIA or in-process test demonstrates: navigate tab 1 to a real
directory, create tab 2 and navigate it elsewhere, click tab 1, and observe
the original column chain/focus (not merely the address label). Add a focused
seam test if practical, but do not broaden the callback API again without
preserving ordinary navigation semantics.

## 37. Dual-pane search routing and treemap DPI input (2026-08-08 continuation)

Two additional static defects were corrected in a bounded UI slice:

* `src/ui/src/ColumnView.cpp:430-463` (`NavigateToHierarchy(const
  std::vector<std::wstring>&, bool)`) previously hardcoded `columns_`,
  `focusedColumnIndex_`, and `verticalScrollOffset_`. Complete search-path
  reconstruction therefore rebuilt the left pane even when the right pane was
  active. The overload now uses `ActiveColumns()` and
  `ActiveFocusedColumnIndex()` and resets the active pane's vertical offset.
  This aligns complete search-result navigation with the existing full-path
  overload and with file operations' active-pane contract.
* `src/ui/src/WindowShell.cpp:1513-1522,1562-1570` now converts physical
  `WM_LBUTTONDOWN`/`WM_MOUSEMOVE` coordinates to DIPs before treemap hit
  testing. Treemap layout/render geometry is DIP-based, so the previous raw
  pixel forwarding was wrong at effective DPI above 96 and could hover/click
  an adjacent tile. The conversion mirrors ColumnView's existing input
  boundary (`CurrentDpiScale`).

Validation:

* Debug `FastFiles` rebuilt successfully.
* Focused `ffnavigation_tests`, `fftreemap_layout_tests`, and
  `ffcolumn_hit_test_tests`: **3/3 passed**.
* Full serial CTest after CRLF normalization: **35/35 passed**, including the
  doc-drift/line-ending guard.
* `/analyze` rebuilt `FastFiles` successfully.
* A safe four-second launch found the exact development UI and sibling engine
  processes alive; both exact paths were stopped afterward. This confirms
  startup/process discovery only, not interactive dual-pane search or treemap
  click behavior.
* No protocol, service, installer, signature-pin, or OpenSpec task state was
  changed.

## 38. Degraded storage aggregate truthfulness (2026-08-08 continuation)

Static inspection found that `StorageAnalysis::RefreshData` marked every
degraded directory `calculating=true`, but `RequestFolderAggregate` requires a
nonzero indexed `volumeRowId`. In the active degraded path those IDs are zero,
so the list could display `Calculating…` forever even after all currently
browsed descendants were available in the snapshot.

The bounded fix adds the header-only seam
`src/ui/include/ffui/SnapshotAggregate.h`:

* `AggregateSnapshotDirectory` recursively sums known file bytes and entry
  counts from the immutable snapshot.
* Missing/access-denied descendants, cycles, or arithmetic overflow produce a
  lower-bound result with `complete=false`; callers must not present it as a
  full-volume total.
* `StorageAnalysis::RefreshData` uses this path for directory entries without
  a durable volume row, computes a complete parent total when possible, and
  renders `Partial` for an incomplete subtree rather than an unbounded spinner.
  Indexed rows retain the existing asynchronous aggregate protocol.

Regression coverage:

* New `tests/ui/test_snapshot_aggregate.cpp` and CTest target
  `ffsnapshot_aggregate_tests` cover complete nested totals, missing
  descendants, and unavailable roots.
* Full serial CTest: **36/36 passed**.
* `/analyze` build: PASS.
* Safe four-second development launch found the exact UI and sibling engine
  alive; both exact processes were stopped afterward.

This is intentionally not a claim that degraded mode can calculate an entire
unbrowsed volume. It makes available data useful and labels the remaining
coverage boundary honestly. A future storage vertical can add bounded
engine-side path aggregation if full-volume degraded analysis is required.

## 39. Direct-path Finder ancestor chain (2026-08-08 continuation)

`ColumnView::NavigateToPath` previously cleared the active pane and created
only one column for the requested leaf. Click navigation correctly retained
ancestors, but address-bar, sidebar, startup-location, storage navigation, and
history paths therefore lost the Finder-style chain. The fix is in
`src/ui/src/ColumnView.h` (`BuildNavigationPathChain`) and
`src/ui/src/ColumnView.cpp:381-423`:

* drive paths become `C:\`, `C:\Users`, …, leaf;
* UNC paths keep `\\server\share` as the root, then append descendants;
* the active pane is rebuilt and every materialized column is requested;
* pending `selectName` is bound to `pendingSelectionPath_`, so a same-named
  item in an ancestor cannot consume the selection intended for the final
  column.

Regression coverage in `tests/ui/test_column_hit_test.cpp` verifies drive and
UNC chains. Full serial CTest after this slice: **36/36 passed**;
`/analyze` build: PASS; safe startup found exactly the UI and sibling engine
alive and both were stopped afterward.

Runtime address-entry proof remains a harness gap: external focus injection
caused the edit control to lose focus before commit. Do not infer that this
static chain fix is runtime-verified until a foreground/UIA or in-process
address-entry smoke commits a real path and observes ancestor columns.

## 40. Explicit application manifest and verification (2026-08-08 continuation)

The repository had no first-party application manifest for the desktop UI,
which left two Windows behaviors undeclared: Common Controls v6 activation and
the process long-path/DPI declarations. A bounded packaging/runtime slice added:

* `src/ui/src/FastFiles.manifest` — Common Controls v6 dependency,
  `PerMonitorV2` DPI awareness, and `longPathAware=true`.
* `src/ui/src/FastFiles.rc` — explicit `RT_MANIFEST` resource 1.
* `src/ui/CMakeLists.txt` — includes the resource and passes
  `/MANIFEST:NO` so MSVC does not emit a second automatic manifest resource.

The first link attempt exposed a genuine build integration issue (`CVT1100:
duplicate resource type MANIFEST/name 1`); it was corrected by disabling only
the automatic linker manifest while retaining the explicit resource. The
current Debug and `/analyze` targets link successfully. A local `mt.exe` was
not available for resource extraction, so the XML/resource contents are
statically verified and compilation/linking is the runtime-adjacent evidence;
do not claim an extracted installed manifest until a Windows SDK `mt.exe` or
equivalent inspection is run.

Validation after the correction:

* `cmake --build --preset debug --target FastFiles`: PASS.
* `cmake --preset analyze && cmake --build --preset analyze --target FastFiles`:
  PASS (including `FastFiles.rc.res`).
* Serial CTest: **36/36 PASS**, including `ffdoc_drift_tests`.
* Safe four-second launch of the exact development
  `build/debug/src/ui/FastFiles.exe` found the sibling
  `build/debug/src/engine/FastFilesEngine.exe`; both exact paths were stopped
  afterward. This proves process startup/discovery only, not interactive UI
  acceptance.

The manifest improves the platform declaration boundary but does not itself
solve explicit `\\?\\` path handling in every UI/API call, network/reparse
semantics, or the privileged service's zero signature pins. Keep those as
separate recovery work.

### Immediate continuation state

The current tree is buildable and has **36/36** serial tests passing. The next
agent should update this file after every material slice and begin with the
highest-impact remaining usability risks, in this order unless new evidence
changes it:

At the time of this update, `git status --porcelain=v1` reports **101 entries**:
**0 staged**, **69 tracked files with worktree edits**, and **32 untracked
files/directories**. This includes the recovery product WIP, OpenSpec/tooling
sync artifacts, diagnostic `.obj` files, and this handoff. No commit, reset,
restore, clean, stash, or branch movement was performed.

1. Complete per-tab **dual-pane** state persistence (the current tab save/restore
   contract preserves only the active pane; `NavigationWorkspace::Tab::panes`
   still lacks a safe public snapshot seam).
2. Replace remaining degraded-storage “unknown”/spinner cases with an explicit,
   bounded engine-side aggregation contract if full-volume storage analysis is
   required; preserve `Partial` truthfulness for unavailable descendants.
3. Exercise/repair the snapshot publication contract (8 MiB capacity cliff,
   raw shared-header publication ordering, and UI generation/retry behavior)
   before promising large-volume indexed search.
4. Build a reliable foreground/UIA or in-process address-entry acceptance seam;
   static ancestor-chain tests are present, but external focus injection did
   not commit an edit in the prior probe.
5. Keep privileged MFT/USN activation blocked on real signed artifacts and
   operational validation; never bypass the all-zero signature pins.

Do not broaden implementation into a rewrite, edit OpenSpec checkboxes, or
remove the dirty recovery artifacts. The exact current source/test changes are
visible in `git status --short`; preserve them before any future implementation
session checkpoint.

## 41. Per-tab dual-pane state slice (2026-08-08 continuation)

The previous handoff correctly called out that the dual-pane implementation
was only saving the active pane. This was a real spec-level defect, not merely a
runtime harness gap: `dual-pane-mode` requires dual-pane to be per-tab, with
independent paths, columns, histories, and one active-pane identity per tab.

The bounded correction is:

* `src/ui/src/NavigationWorkspace.h/.cpp` now exposes the active tab's
  `PaneContext(index)`, `ActivePaneIndex()`, and `ActivePaneCount()` accessors.
  The index is checked with `vector::at` so invalid shell bookkeeping fails
  loudly rather than silently mutating another context.
* `src/ui/src/ColumnView.h/.cpp` adds `SavePaneState` and `RestorePaneState` for
  a specific pane without changing input focus. The existing active-pane APIs
  delegate to these methods. Restore skips the synthetic empty root when
  issuing engine requests.
* `WindowShell::SaveActiveTabViewState` now stores both pane column chains,
  focused-column indices, scroll offsets, and the focused path for the active
  tab. `RestoreActiveTabViewState` first aligns `ColumnView::SetDualPane` with
  the target tab, reconstructs each pane (including a synthetic This-PC root),
  and restores the target active pane.
* All tab transitions (mouse tab/new/close, keyboard next/previous/new/close,
  reopen, and sidebar-open-in-new-tab) now use the common restore boundary.
  A tab that is single-pane no longer leaves the previous tab's dual renderer
  enabled.
* `ColumnView::SetDualPane(false)` promotes `columns2_` and its focus/scroll
  state into the single-pane slot when the right pane was active. This matches
  the workspace specification that disabling dual-pane retains the previously
  active context.
* `tests/navigation/test_navigation_workspace.cpp` covers both independent
  pane contexts, active-pane identity, and a separate single-pane tab.

Validation:

* Focused navigation/hit-test tests: **2/2 PASS**.
* Full serial CTest: **36/36 PASS**.
* `/analyze` Debug build: PASS.
* Safe four-second launch found the exact development UI and sibling engine;
  both exact paths were stopped afterward. This is startup/process evidence,
  not interactive tab restoration proof.

The remaining runtime evidence boundary is unchanged: a reliable foreground
UIA or in-process smoke still needs to navigate two real tabs, make one tab
dual-pane with different directories, switch away and back, and inspect both
column chains and the active pane. Static state ownership is now aligned with
the spec, but do not label it runtime-verified until that smoke exists.

## 42. Bookmark usability slice (2026-08-08 continuation)

Bookmark persistence and mutation methods already existed, but no command or
sidebar interaction reached them. The user-facing feature was therefore
effectively disconnected. The bounded correction adds:

* `navigation.add-bookmark` to `src/ui/src/CommandSystem.cpp`, with the
  conventional `Ctrl+D` binding and command-palette discovery.
* A `WindowShell` handler that adds the active pane's real folder path,
  flushes the workspace state, refreshes the sidebar, and repaints. The
  synthetic This-PC root is intentionally ignored because it is not a folder.
* Case-insensitive Windows-path duplicate suppression in
  `NavigationWorkspace::AddBookmark`; adding the same folder twice no longer
  creates ambiguous duplicate rows.
* A native sidebar `WM_CONTEXTMENU` for bookmark rows in
  `src/ui/src/NavigationSidebar.cpp`: Rename (using the existing safe leaf-name
  prompt), Remove, Move Up, and Move Down. Each successful mutation flushes
  state and rebuilds the visible rows. The menu is restricted to user
  bookmarks; Drives and Known Folders remain non-editable.
* Regression coverage for duplicate suppression and independent bookmark/tab
  state in `tests/navigation/test_navigation_workspace.cpp`.

Validation:

* Focused `ffnavigation_tests` and `ffcommand_tests`: **2/2 PASS**.
* Full serial CTest: **36/36 PASS**.
* `/analyze` Debug build: PASS.
* Safe four-second development launch found the exact UI and sibling engine;
  both exact paths were stopped afterward. Interactive bookmark menu/prompt
  behavior remains UI-runtime evidence rather than a static-test claim.

The sidebar row geometry still uses its existing owner-drawn/DPI boundary;
that is separate from bookmark mutation routing and should be checked during
the next real UIA pass.

## 43. Degraded treemap aggregate plumbing (2026-08-08 continuation)

`TreemapView::BuildTree` previously assigned `totalSizeBytes=0` and
`calculating=true` to every degraded directory. Because degraded entries have
no durable `volumeRowId`, no privileged aggregate request could resolve those
nodes, so even a fully browsed subtree never received a meaningful tile size.

The bounded correction in `src/ui/src/TreemapView.cpp` reuses
`ffui::AggregateSnapshotDirectory` for directory nodes with `volumeRowId==0`:

* complete browsed subtrees receive their recursive nonzero total immediately;
* missing/access-denied/cyclic descendants remain marked calculating/partial,
  preserving the lower-bound truthfulness contract;
* privileged rows retain the existing asynchronous folder-aggregate path;
* recursive sums now saturate at `UINT64_MAX` instead of wrapping on overflow.

This changes data plumbing only; treemap squarification/layout math and the
closed IPC/service protocol are untouched. Existing aggregate tests already
cover complete nested totals, incomplete descendants, and unavailable roots;
the focused treemap-layout and snapshot-aggregate tests both pass. A rendered
pixel/UIA treemap acceptance remains outstanding because the D2D surface has no
UIA provider.

## 44. Indexed-search seam forensic audit (2026-08-08 continuation)

The next highest-impact usability gap is not merely a slow query: the current
desktop has no UI-to-engine indexed-search request/response path. This was
verified by tracing both sides rather than relying on scope labels:

* `src/ui/src/SearchPanel.cpp:331-365` parses the query locally with
  `ffsearch::ParseQuery`, sets the scope/sort/cap, and queues one worker job.
* `SearchPanel::WorkerMain` (`SearchPanel.cpp:367-388`) always calls
  `PopulateCandidates` followed by `ffsearch::ExecuteSearch`; no EngineClient
  request is made.
* `SearchPanel::PopulateCandidates` (`SearchPanel.cpp:813-881`) obtains only
  `engine_->ReadSnapshot()`, materializes every successful snapshot directory
  and entry into a local vector, then caches that vector per snapshot epoch.
* `persistentCoverage_` (`SearchPanel.cpp:768-805`) only changes the labels and
  the local `persistentOnly` predicate. It does not switch the data source to
  SQLite or the projection. A nonzero `volumeRowId` in a published snapshot is
  evidence that indexed rows were merged into the snapshot, not evidence that
  the whole persistent index is queried.
* `src/protocol/include/ffprotocol/UiProtocol.h:15-31` ends the client-request
  message set at `SetIndexingPaused`; there is no search request or result
  message. `EngineClient.h:63-75` likewise exposes directory, aggregate,
  volume-status, and pause operations only. `EngineClient.cpp:253-390` has no
  indexed-search reader dispatch.
* `src/engine/src/UiServer.h:35-65` has callbacks only for reload, unavailable
  volumes, aggregates, volume status, and pause. `UiServer.cpp:394-478` rejects
  all other client message types as protocol violations.
* `src/engine/CMakeLists.txt:14` links `FastFilesEngine` to
  `ffprotocol ffipc ffsetup ffindexstore`, not `ffsearch`; the engine therefore
  cannot currently reuse the query parser/predicate implementation without an
  explicit dependency decision.
* `src/indexstore/include/ffindexstore/Store.h:116-134` exposes streaming
  `ForEachEntry`, identity listing, count, and folder aggregate only. There is
  no prepared, bounded search operation. `Store.cpp:572-631` streams rows
  without path reconstruction; `Store.cpp:633-691` only implements a recursive
  aggregate CTE.
* `src/engine/src/IndexPipeline.h:116-122` exposes snapshot export and folder
  aggregate only. `IndexPipeline.cpp:354-414` reconstructs paths while exporting
  directory snapshots; this is the existing path-building logic, not a query
  service.

### Current truth

| Claim | Verdict | Evidence |
| --- | --- | --- |
| Search is fast/in-process for already browsed data | **CONFIRMED, bounded** | `SearchPanel::PopulateCandidates` caches one snapshot projection; `ExecuteSearch` runs on the worker thread. |
| “All Indexed Locations” searches the durable index | **FALSE/OVERCLAIMED if read literally** | It still scans the mapped snapshot vector; there is no UI protocol message or engine query callback. |
| Broad indexed labels are honest about degraded coverage | **CONFIRMED** | `SearchPanel.cpp:792-805` labels broad scopes as indexed only when a snapshot contains nonzero volume rows; otherwise it says browsed locations. This is honest about the dataset actually present, but not a substitute for persistent querying. |
| Persistent-only filtering prevents degraded rows from broad results | **CONFIRMED locally** | `Search.cpp:22-29` and `SearchPanel.cpp:351-356` filter candidates marked `persistentIndexed=false`; this does not enlarge coverage. |
| Full-volume query is ready to add by calling `Store::ForEachEntry` | **UNKNOWN / unsafe assumption** | Rows contain parent FRNs, not full paths. A correct scope/path result requires bounded parent-chain reconstruction or an engine-side path index; naive per-row reconstruction is potentially O(entries × depth). |

### Constraints for the future implementation slice

1. Do not call the privileged service or alter its protocol. Indexed search must
   remain an engine/UI same-privilege feature and must continue to fall back to
   degraded snapshot search whenever durable coverage is absent or stale.
2. Preserve the existing `ffsearch` semantics (name/folder/ext/kind/size/date
   filters, wildcard matching, relevance/sort, cancellation, and the 10,000 UI
   result cap). Sending only `primaryTerm` would silently regress filters.
3. The UI pipe has a 1 MiB frame ceiling (`ffprotocol::kMaxFrameSize`). A
   response containing 10,000 UTF-16 paths can exceed one frame. The design
   must either use a substantially smaller byte-bounded result page or add a
   validated chunk/continuation protocol; never allocate from an unchecked
   count and never silently truncate without a result flag.
4. Query text and scope paths need explicit UTF-16 length bounds matching the
   existing frame/path validation. Sort, direction, scope, request ID, and
   cancellation/generation must be fixed-width validated fields.
5. Engine-side query execution must not hold `IndexPipeline::mutex_` across a
   long UI pipe write. The store/projection lock and response serialization
   need an explicit lifetime/cancellation policy.
6. `ffsearch` currently uses Windows `CompareStringOrdinal` and Win32 time
   parsing (`src/search/src/Query.cpp`), so linking `ffsearch` into the engine
   is a real portability/build decision, not a header-only include. An
   alternative is to introduce a shared, platform-neutral query plan and keep
   UI predicates out of the engine; this requires a proposal before coding.
7. Durable rows can be incomplete, unavailable, or mid-reconciliation. A
   persistent result must carry coverage/availability truth (or an explicit
   “indexed but possibly stale/partial” status) so the UI never presents a
   snapshot-like broad scope as authoritative when the engine knows otherwise.

### Smallest technically sound future vertical

The next OpenSpec change should first establish a bounded engine query seam and
tests, before attempting UI polish:

* Define a versioned `RequestIndexedSearch` / `IndexedSearchResult` UI message
  pair with request ID, query text, scope path, scope/sort/direction, maximum
  serialized response bytes, and explicit `Available/Unavailable/Invalid/
  Truncated/Cancelled` status. Keep all lengths/counts checked before
  allocation and reject malformed frames.
* Add an engine/store query-plan API that can produce `ffsearch::Candidate`
  equivalents with full reconstructed paths under a documented cost bound.
  Prefer one bounded traversal/snapshot of the projection or a recursive SQLite
  query over per-row unbounded path walks; record cancellation checkpoints.
* Add protocol/parser tests, store scope/filter tests, and an EngineClient ↔
  UiServer integration test with an in-memory/temp database. A test that merely
  calls `ExecuteSearch` over a fixture vector is not integration evidence.
* Integrate `SearchPanel` only after the seam exists: indexed request when the
  selected scope has durable coverage, degraded worker fallback otherwise; keep
  the label and status tied to the response coverage, not just `engineActive_`.

Until that vertical is implemented and runtime-tested, the honest product
statement is: **Fast Files searches the currently published/browsed snapshot,
with bounded worker execution and truthful degraded labels; it does not yet
search the durable index end-to-end.**

This section supersedes the earlier immediate-continuation wording that still
listed “complete per-tab dual-pane state persistence” as pending. Section 41
documents that slice as implemented and statically tested; only its interactive
UIA acceptance remains outstanding.

## 45. Bounded persistent indexed-search vertical (2026-08-08 continuation)

The previously documented indexed-search gap is now wired end-to-end for
durable rows. This is a product-code change, not an OpenSpec checkbox update.
The privileged service protocol and all signature/ACL invariants remain
untouched.

### Wire contract

`src/protocol/include/ffprotocol/UiProtocol.h:17-35, 190-281` adds the closed
same-privilege UI messages `RequestIndexedSearch=17` and
`IndexedSearchResult=18` plus fixed headers and host-side request/candidate/
response models. `src/protocol/src/UiProtocol.cpp:64-326` now provides:

* checked scope/sort/status validation;
* request parsing with nonzero request IDs, bounded query/path lengths,
  bounded result count, reserved-byte checks, exact payload-size validation,
  and overflow-safe arithmetic;
* response parsing that checks status/flags/count before `reserve`, validates
  every variable-length candidate, and rejects trailing bytes;
* response serialization capped to `kMaxFrameSize - sizeof(FrameHeader)`.

The UI cap remains 10,000 requested rows, but a variable-size response is
limited to 4,096 candidates and/or the 1 MiB payload budget. The serializer
sets `truncated` when either bound is reached; it never emits an over-limit
frame or silently claims completeness.

### Engine path

`src/engine/src/IndexPipeline.h:123-131` and
`src/engine/src/IndexPipeline.cpp:435-562` add `SearchIndexed`. It:

* validates the request again at the engine boundary;
* copies candidate fields while holding `IndexPipeline::mutex_`, then releases
  the lock before parsing/matching/sorting;
* derives mounted drive prefixes from active volume rules or durable volume
  identity (`ResolveDriveLetterForVolumeKey`);
* skips unavailable/disabled volumes;
* reconstructs parent paths from FRN chains, including empty/synthetic NTFS
  root names, and exposes only candidates with a complete trusted parent path;
* reuses the existing `ffsearch` parser/matcher/sort semantics, including
  wildcard/name/folder/size/date/kind predicates and persistent-only filtering;
* returns `Unavailable` if no searchable mounted durable volume is available,
  `InvalidRequest` for bad direct calls, and `Completed` otherwise.

`src/engine/CMakeLists.txt:14` links `ffsearch` into the engine. `Main.cpp`
connects `UiServer::onRequestIndexedSearch` to the pipeline. Disabled volume
rules are explicitly excluded so a stale disabled configuration cannot leak
old rows into “All Indexed Locations”.

### UI path and fallback

`src/engine/src/UiServer.h/.cpp` parses and dispatches only the new validated
request and serializes one bounded response. `src/ui/src/EngineClient.h/.cpp`
adds request-ID keyed callbacks, reader dispatch, and unavailable callbacks on
pipe loss/write failure so an in-flight query cannot remain pending forever.

`src/ui/src/SearchPanel.h/.cpp` routes broad indexed scopes (Current Folder +
Indexed Subfolders, Current Drive, All Indexed Locations) to the engine when
`persistentCoverage_` is true. It posts the response back to the owner window,
preserves engine sort order, and navigates using the returned full folder path.
If the engine/index is unavailable, the exact existing worker path is invoked
once with an explicit “Indexed search unavailable; searching browsed snapshot”
state. Current-folder “Available Data” remains snapshot-backed by design.
`WindowShell.cpp` handles `WM_APP_INDEXED_SEARCH_COMPLETE`.

### Tests and evidence

* `tests/protocol/test_protocol.cpp` covers message registration, request/
  response round-trips, malformed trailing bytes, zero result limits, and
  serializer frame-budget truncation.
* `tests/engine/test_index_pipeline.cpp` covers durable projection matching,
  reconstructed `C:\\Users` parent paths, wildcard/current-folder scope, invalid
  request rejection, and disabled-volume exclusion.
* Debug build: PASS.
* Serial CTest: **36/36 PASS** (the first run exposed only CRLF drift; touched
  files were normalized and the rerun passed `ffdoc_drift_tests`).
* `/analyze` build: PASS after replacing an aggregate-initialization pattern
  that MSVC C6001 could not prove initialized.
* Safe development launch: exact `build/debug/src/ui/FastFiles.exe` and sibling
  `build/debug/src/engine/FastFilesEngine.exe` both appeared and were stopped
  afterward. This proves startup/discovery, not a live indexed-result query.

### Remaining evidence boundary

The real service remains fail-closed behind placeholder signature pins, so a
fresh installation cannot populate a live MFT/USN index. A runtime indexed
query therefore still requires a pre-existing signed/validated index database
and a mounted volume identity; do not bypass authentication to manufacture
that evidence. The query currently materializes all searchable projection
entries per request, so large-index latency/memory and cancellation remain a
future performance vertical. The result is bounded and correct, but not yet a
WizFile-scale 10M-entry implementation.

## 46. Final transfer block for a fresh agent (2026-08-08)

This section is the authoritative session-transfer checkpoint. It is written
for an agent that has not seen the preceding conversation. Read this section,
then read `AGENTS.md`, then inspect the cited symbols before changing anything.

### 46.1 Objective and non-negotiable safety rules

The user asked for a forensic/recovery handoff for Fast Files, preserving the
valuable dirty working tree while continuing toward a genuinely usable native
Windows file manager. The current tree is intentionally not a clean baseline.
Do not reset, restore, checkout, clean, stash, revert, rebase, merge,
cherry-pick, switch, or commit. Do not delete untracked files. Do not rewrite
the zero-valued Authenticode pins or bypass peer authentication. Do not read
`verify/runs/`, `verify/baselines/`, or `verify/.signing/`. Do not install a
service or elevate merely to obtain evidence. Any future mutation must be
explicitly scoped, reversible, and reviewed against the existing WIP.

OpenSpec Explore was the original investigation mode; the present WIP has
already implemented several narrowly scoped recovery slices. Treat existing
code and this handoff as evidence, not as permission to mark OpenSpec tasks
complete. `recovery-degraded-browsing/tasks.md` remains unchecked, and
`close-independent-validation-gaps/tasks.md` remains the current open-work
source of truth.

### 46.2 Exact repository checkpoint

Static Git evidence captured immediately before this block:

* branch: `main`;
* HEAD: `52d33da0291f7914dbd7deabbd6e70f27ed808e0`;
* HEAD subject: `Align docs, tooling, and tests with the shipped architecture`;
* staged paths: none;
* `git status --short`: 107 entries (75 tracked modifications and 32
  untracked entries at capture time);
* tracked diff: 74 files, 3,396 additions and 491 deletions;
* important untracked artifacts include this handoff, the recovery report,
  `ENGINEERING_AUDIT_REPORT.md`, the two OpenSpec change directories,
  `src/ui/src/EngineDiscovery.{h,cpp}`, `tests/ui/test_engine_discovery.cpp`,
  added UI helper headers/tests, diagnostic `.obj` files, and agent-tooling
  directories. Preserve them all until provenance is deliberately classified.

The large tracked delta mixes the interrupted recovery/product work with
OpenSpec tooling/document synchronization. Do not equate the full 3,396-line
addition count with one feature. Use `git diff -- <path>` and the earlier
sections of this file to isolate product slices. `git diff --cached` is empty.

### 46.3 What this continuation implemented

The latest slice is a bounded persistent indexed-search vertical. It is not a
claim that the whole application is production-ready.

Protocol contract:

* `src/protocol/include/ffprotocol/UiProtocol.h` adds message types 17/18,
  bounded request/result constants, packed request/result/candidate headers,
  and host request/response models.
* `src/protocol/src/UiProtocol.cpp` validates every length/count/status/flag,
  rejects trailing bytes and malformed records, checks arithmetic before
  allocation, and caps serialization to the one-megabyte protocol frame.
  Candidate results are explicitly marked `truncated` when the 4,096-candidate
  or frame budget is reached.
* `tests/protocol/test_protocol.cpp` covers round trips, malformed trailing
  data, zero limits, and frame-budget truncation.

Engine contract:

* `src/engine/src/IndexPipeline.{h,cpp}` adds `SearchIndexed`. It validates at
  the engine boundary, copies projection fields under the pipeline mutex, then
  releases the mutex before parsing/matching/sorting.
* It reconstructs full paths from FRN parent chains, handles synthetic/empty
  NTFS root names, derives mounted drive prefixes from active volume rules or
  durable volume identity, and excludes unavailable or disabled volumes.
* It reuses `ffsearch` persistent-only predicates and returns explicit
  `InvalidRequest`, `Unavailable`, or `Completed` status. The root/disabled
  volume regressions found during review are fixed and covered by
  `tests/engine/test_index_pipeline.cpp`.
* `src/engine/src/Main.cpp` wires the callback into `UiServer`.

IPC/UI contract:

* `src/engine/src/UiServer.{h,cpp}` accepts only the validated request and
  emits one bounded result response; malformed protocol input closes the
  connection.
* `src/ui/src/EngineClient.{h,cpp}` tracks callbacks by request ID, dispatches
  result frames, and completes outstanding requests as unavailable on pipe
  loss/write failure so a query cannot hang forever.
* `src/ui/src/SearchPanel.{h,cpp}` routes broad indexed scopes to the engine
  when persistent coverage is advertised. If the engine/index is unavailable,
  it invokes the existing snapshot worker once with an honest degraded status.
  Current-folder “Available Data” deliberately remains snapshot-backed.
* `src/ui/src/WindowShell.cpp` consumes the indexed-search completion message.
  The completion message is `WM_APP + 13`; `WM_APP + 12` is already used by the
  treemap aggregate callback and must not be reused.

### 46.4 Verified evidence at this checkpoint

All evidence below is static/build/test evidence unless explicitly labelled
runtime. It does not prove an authenticated live MFT/USN service.

* Debug build of the current working tree: PASS.
* Serial CTest from `build/debug`: **36/36 PASS** on the final rerun,
  including protocol fuzz/diagnostics, engine indexed-search tests, watcher,
  snapshot, UI helper, UIA PowerShell, intake gate, setup security descriptor,
  and document-drift tests. Total time was about 20 seconds.
* Latest `build/analyze` build after the root/disabled-volume corrections:
  PASS (9 rebuilt/linked targets; no `/analyze` diagnostics).
* `git diff --check` for all indexed-search product/test paths: PASS.
* Safe development launch: exact sibling binaries
  `build/debug/src/ui/FastFiles.exe` and
  `build/debug/src/engine/FastFilesEngine.exe` appeared with expected paths;
  they were stopped by exact process identity afterward. This proves startup
  discovery/launch only, not interactive folder navigation or a live indexed
  result.

The first document-drift run failed only because touched files had mixed LF/
CRLF endings; the touched files were mechanically normalized and the complete
36-test rerun passed. Do not normalize unrelated dirty files just to silence
Git warnings; current warnings identify pre-existing files such as
`SnapshotPublisher.h`, `ui/Main.cpp`, `Preview.cpp`, `SettingsDialog.cpp`,
and the Authenticode test.

### 46.5 Current truth: works, partial, and unverified

Confirmed or strongly evidenced working:

* CMake/MSVC debug and `/analyze` compilation with strict warnings.
* Protocol framing/parsing/fuzz boundaries and the new indexed-search wire
  contract.
* Engine projection path reconstruction, wildcard/scope matching, disabled
  volume exclusion, and bounded response serialization in unit tests.
* Engine discovery candidate validation/launch and basic UI-pipe connection
  from the development layout.
* Existing degraded enumeration, bounded watcher implementation, snapshot
  publication tests, file-operation tests, preview tests, and security parser
  tests to the extent represented by CTest.
* Shell copy/cut/paste uses `CF_HDROP`; double-click file opening and several
  synthetic-root/history/tab/treemap/storage plumbing slices are present in the
  dirty tree (see §§21–43 for exact evidence and limitations).

Partial or dangerous:

* Broad search now has a real engine seam, but each request materializes and
  path-reconstructs the searchable projection. It is bounded, not yet
  WizFile-scale; no cancellation or indexed-query latency benchmark exists.
* UI folder navigation has code paths from activation through `RequestColumn`
  and `UiServer::HandleRequestDirectory`, but earlier diagnostics recorded
  `REQUEST_DIR_COUNT=0`; interactive mouse enqueue/send remains an evidence
  gap. At DPI above 96, WM coordinates are passed to DIP hit-testing without
  conversion (`WindowShell.cpp`), and queue requests can be dropped while the
  pipe is not valid (`EngineClient.cpp`).
* Column selection preservation is index-based after snapshot refresh and can
  retarget a file after watcher reorder; this is a file-safety P1 even though
  tests pass.
* Column View has no vertical scroll offset; rows beyond the viewport are not
  mouse-visible. Details overlay geometry can cover columns while hit-testing
  still routes clicks to ColumnView. Storage owner-data notification routing,
  aggregate IDs, treemap data plumbing, tabs, dual-pane, settings, properties,
  and shell parity have substantial partial/disconnected behavior described in
  prior sections.
* Snapshot publication remains two 8-MiB slots with raw shared-header reads;
  capacity cliff and reader consistency risks remain architectural work.

Unverified or intentionally inactive:

* The privileged service/MFT/USN path has never been validated end-to-end in
  this environment because `src/setup/include/ffsetup/PinnedSignatures.h`
  contains placeholder/zero pins and authentication correctly fails closed.
  Never replace the pins or weaken verification for a test.
* No signed live index was available for a runtime indexed-search query.
* Interactive UIA acceptance for click-drive → next-column, 5-level browsing,
  search result opening, watcher refresh, and engine-loss status must be rerun
  explicitly; a launch smoke is not an acceptance test.

### 46.6 Known immediate P0/P1 blockers

1. **P0 usability evidence gap / likely input path defect:** the recorded
   diagnostic had engine launch, UI connection, and subscribe success but zero
   directory requests. Instrument or exercise the full edge sequence before
   claiming browsing works: WM input → hit-test → `ColumnView::ActivateItem`
   → `RequestColumn` → `EngineClient::RequestDirectory` → pipe write →
   `UiServer::HandleRequestDirectory` → degraded enumerate → snapshot publish
   → UI refresh. Do not stop at a method existing in source.
2. **P1 selection safety:** `ColumnView::RefreshColumnFromSnapshot` preserves
   numeric row indices, not stable identity/path; watcher insertion/reordering
   can cause copy/delete/move to target a different file.
3. **P1 navigation geometry:** no vertical scroll offset, DPI mismatch above
   96, and the opaque details card is not subtracted from viewport/hit-testing.
4. **P1 index scale:** `SearchIndexed` scans/materializes all projection rows
   on every request; response bounds prevent protocol blowups but not latency
   or memory spikes on millions of entries.
5. **P1 snapshot scale/consistency:** fixed 8-MiB slots and non-transactional
   shared-header read patterns can produce stale/failed publication or require
   a redesign before large indexes are credible.
6. **P1 product capability gate:** privileged MFT/USN ingestion remains
   unavailable until real signed pins and live-service validation are supplied
   through the secure release process.

### 46.7 Exact next-session opening procedure

Run from a Visual Studio developer PowerShell, but do not mutate Git:

```powershell
Set-Location D:\Documents\tryPython\FastFiles
Get-Content .\AGENTS.md
Get-Content .\FASTFILES_FRONTIER_HANDOFF.md -Tail 260
git status --short
git diff -- src/protocol/include/ffprotocol/UiProtocol.h src/protocol/src/UiProtocol.cpp src/engine/src/IndexPipeline.cpp src/ui/src/SearchPanel.cpp
```

Then establish the toolchain and reproduce the final gate:

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$dump = cmd.exe /c "call `"$vs`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $dump) { if ($line -match '^(.*?)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') } }
$env:FASTFILES_NINJA_EXE = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $cmake --build build/debug --parallel 1
& $ctest --test-dir build/debug --output-on-failure -j 1
& $cmake --build build/analyze --parallel 1
```

Before a new implementation slice, inspect the dirty diff and create a
recoverable checkpoint according to the user's chosen policy (WIP branch,
patch export, or WIP commit). This handoff deliberately does not execute that
Git mutation. Never use a broad path or an inferred glob for cleanup.

### 46.8 Recommended next implementation slice (small and testable)

The shortest next slice is an **interactive degraded-navigation acceptance
seam**, not a security or index rewrite:

* instrument a bounded request lifecycle with request IDs and explicit
  “queued/sent/received/failed” counters or diagnostics;
* preserve queued directory requests until the pipe is connected, or return a
  visible retry state rather than silently dropping them;
* add a deterministic UI/engine integration test that drives a synthetic root
  and one real temporary directory through the same request/response seam;
* fix coordinate conversion at non-96 DPI and subtract the details panel from
  navigation hit-testing/visibility;
* replace row-index selection preservation with stable path/FRN identity;
* add an acceptance harness assertion for a received directory request and a
  populated next column. Keep all security invariants untouched.

Non-goals for that slice: changing Authenticode pins, enabling the service,
redesigning snapshot format, implementing a full treemap, or optimizing the
10M-entry query path. Those need separate OpenSpec changes and independent
acceptance evidence.

Acceptance criteria for the next slice:

1. A cold-start drive activation cannot be lost while the UI pipe is starting.
2. A temporary directory root produces one received directory request and a
   populated child column in an integration/UI test.
3. The same test passes at 96 DPI and a non-96 DPI scale.
4. A watcher reorder does not change the selected stable path.
5. Engine disconnect reports a bounded visible failure and does not leave an
   in-flight request pending forever.
6. `ctest --test-dir build/debug --output-on-failure -j 1` remains 36/36 (or
   the documented count after intentionally adding the new test), and the
   `/analyze` build remains warning-clean.

### 46.9 Protected files and invariants

Treat these as protected until a dedicated review says otherwise:

* `src/setup/include/ffsetup/PinnedSignatures.h`,
  `AuthenticodeVerification.cpp`, service ACL/install code, and all
  Engine↔IndexSvc authentication/protocol code;
* `src/protocol/include/ffprotocol/Commands.h`, frame/parser bounds, opaque
  connection-scoped handles, and the no-generic-path-primitive rule;
* MFT `$DATA` exclusion in `MftParser`, `VolumeScanner`, and USN journal
  correctness code;
* tested file-operation safety, installer ACL hardening, DLL-search-path
  hardening, and the existing verification harness;
* existing renderer/device-loss and file-operation code unless a failing
  acceptance test directly implicates it.

### 46.10 Transfer summary in one screen

```text
HEAD 52d33da; main; nothing staged; 107 dirty status entries.
36/36 serial CTest PASS; latest /analyze build PASS; diff-check PASS.
Latest WIP: bounded protocol -> Engine SearchIndexed -> UI callback/fallback.
Engine discovery/launch works in dev layout; privileged service is fail-closed.
Main immediate blocker: interactive drive click/request path still unproven;
diagnostics previously showed REQUEST_DIR_COUNT=0.
Other P1s: selection retargeting, DPI/hit-test mismatch, no vertical scroll,
details overlay geometry, snapshot 8-MiB/race risk, O(N) indexed query.
Do not touch pins/security or destroy dirty work.
Next exact slice: add a deterministic interactive/UIA acceptance seam for
drive activation and a populated child column; do not treat the old
REQUEST_DIR_COUNT=0 run as current truth.
```

## 47. Latest continuation update (2026-08-08)

This section supersedes stale statements above where they conflict. The
working tree remains intentionally dirty and no Git mutation was performed.

### 47.1 Validation just completed

* `cmake --build build/debug --parallel 1`: PASS (Ninja reported no work).
* `ctest --test-dir build/debug --output-on-failure -j 1`: **36/36 PASS**,
  including protocol fuzzing, file-operation E2E, engine discovery, watcher,
  snapshot, navigation, UI helper, PowerShell, and doc-drift tests.
* `cmake --build build/analyze --parallel 1`: PASS on the latest prior run
  after the same source changes; no `/analyze` diagnostics. Re-run this exact
  command if a future edit changes any of the touched files.
* `git diff --check` for the latest UI/navigation files: PASS.
* A safe development launch previously produced `FastFiles.exe` and its
  sibling `FastFilesEngine.exe`; exact executable paths were terminated and
  no matching processes remained. No user files were touched.

### 47.2 EngineClient pipe-lifecycle hardening now in the tree

Files: `src/ui/src/EngineClient.h` and `src/ui/src/EngineClient.cpp`.

`EngineClient::WriteCurrentFrame` now serializes the active pipe handle under
`writeMutex_`, writes while the handle is protected, and on failure atomically
retires the exact failed handle. `EngineClient::ClosePipe` cancels I/O and
closes the active and retired handles under the same mutex. Connection startup
publishes a pipe only while `running_` is true and no other pipe is active.
All UI writers (directory, aggregate, volume status, pause, indexing config,
unavailable-volume, and indexed-search requests) use this helper. Directory
requests remain in the queue when a write fails and are retried after bounded
reconnect; they are no longer lost by a check-then-close race. This is a
concurrency/lifetime fix, not proof of mouse hit-testing.

### 47.3 Explicit Up/Backspace navigation fix

Files: `src/ui/src/NavigationWorkspace.h/.cpp`, `src/ui/src/WindowShell.cpp`,
`tests/navigation/test_navigation_workspace.cpp`.

The pure `ParentNavigationPath` helper distinguishes a normal parent, a drive
root whose target is the synthetic This-PC root (optional containing an empty
string), and a synthetic root with no parent (null optional). The
`navigation.up` command now calls this helper, navigates the active pane, and
refreshes the column/chrome/sidebar state. Tests cover drive child, drive root,
UNC child/root, and synthetic root. Focused navigation tests pass.

### 47.4 Runtime evidence correction

The older diagnostic line `REQUEST_DIR_COUNT=0` was an evidence gap from a run
that did not exercise the corrected click seam. Current diagnostics at
`%LOCALAPPDATA%\\FastFiles\\logs\\diagnostics.log` repeatedly show the full
sequence for temporary nested paths:

```text
ui-directory-request-queued
ui-directory-request-sent
engine-directory-request-received
ui-snapshot-column-refresh
```

Therefore the engine request/response path is **CONFIRMED** for exercised
programmatic/runtime navigation, including nested temporary paths. The exact
physical `WM_LBUTTONDOWN -> hit-test -> ActivateItem` edge remains
**NOT VERIFIED** by UI automation; do not claim it is broken solely from the
old zero-count evidence.

### 47.5 Reclassification of stale prior P1 claims

Static inspection of the current tree shows that several previously reported
defects have already been addressed in this dirty work and must not be handed
off as current facts:

* `ColumnView::ScrollVertical` and `EnsureFocusedItemVisible` apply per-pane
  vertical offsets; `WindowShell` routes wheel/keyboard reveal. “No vertical
  scroll” is stale (interactive long-directory coverage remains unverified).
* `ColumnView::RefreshColumnFromSnapshot` preserves selected/focused names and
  reselects by case-insensitive identity, not raw row indices. The old watcher
  reorder retargeting claim is stale; duplicate-name/FRN edge cases still need
  dedicated tests.
* `WindowShell::CurrentDetailsLayout`/`ComputeDetailsLayout` reserves the
  details pane; navigation viewport width, visibility, and hit-testing use the
  available column width. The old unconditional details-overlay claim is stale
  for this implementation (visual/UIA proof remains useful).
* `WindowShell` converts physical mouse coordinates to DIP before ColumnView
  hit-testing. The old blanket “DPI mismatch” claim is stale; non-96-DPI
  acceptance is still missing.
* `WindowShell::navigation.up` now has explicit root semantics as described in
  §47.3; the prior “Up absent” claim is stale.

### 47.6 Current blockers after these fixes

1. **Interactive acceptance seam (P1 usability/evidence):** no deterministic
   UIA or integration test proves a real mouse click on a rendered drive/folder
   row reaches `ColumnView::ActivateItem`. Direct2D rows are not native
   controls. Add a test seam before claiming full daily usability.
2. **Persistent indexed-search scale (P1 performance):**
   `IndexPipeline::SearchIndexed` snapshots/reconstructs all projection paths
   for each bounded query. It is functionally wired and tested, but not a
   WizFile-scale low-latency implementation; add cancellation, candidate
   limits/telemetry, and benchmarks as a separate change.
3. **Snapshot consistency/capacity (P1 architecture):** fixed 2 x 8-MiB
   shared-memory slots and raw header/data-size reads remain scale and
   publication-order risks. Do not “fix” by weakening synchronization; design
   a versioned/read-retry format separately.
4. **Privileged index activation (P0 product gate):**
   `src/setup/include/ffsetup/PinnedSignatures.h` still contains zero
   placeholders, so mutual authentication intentionally fails closed. MFT/USN
   code is present and unit-tested but not release/runtime validated.
5. Network/UNC, long-path, reparse, removable-volume behavior and full
   interactive storage/treemap acceptance remain incomplete or unverified.

### 47.7 Exact next-session opening procedure

Read `AGENTS.md`, this handoff, then inspect only the relevant dirty diff. Do
not re-run broad OpenSpec generators (they can refresh tooling mtimes). Re-run:

```powershell
cmake --build build/debug --parallel 1
ctest --test-dir build/debug --output-on-failure -j 1
cmake --build build/analyze --parallel 1
git diff --check
```

Then inspect `NavigationChrome::HandleMessage`/tab command routing and build a
bounded acceptance seam for one temporary directory: launch UI/engine,
navigate synthetic root -> drive -> temp root -> child, assert diagnostics
contain queued/sent/received/refresh, and verify a second column has the child.
Keep this test non-destructive and avoid `verify/runs`, `verify/baselines`, and
`verify/.signing`.

## 48. Continuation slice: settings rule editor and acceptance-harness repair (2026-08-08)

This section is the newest continuation state and supersedes the recommended
next slice in §47.7 where it conflicts. No Git mutation, cleanup, reset, or
security change was performed.

### 48.1 Product-code changes made

Files changed:

* `src/ui/src/SettingsDialog.h`
* `src/ui/src/SettingsDialog.cpp`

The Indexing page now has a visible `ruleList_` with `Rule` and `Path`
columns. Selecting a volume repopulates that list from the matching
`ffprotocol::VolumeSetting::rules` entry. The old `RemoveVolumeRule`,
`MoveVolumeRuleUp`, and `MoveVolumeRuleDown` no-op bodies are now connected to
the selected rule and preserve ordered-prefix semantics. The move commands
reselect the moved row; all mutations call the existing `onChanged_` callback,
so the existing atomic settings write/reload path remains authoritative.

`AddVolumeRule` now targets the selected volume instead of always appending to
`settings_->indexing.back()`. If no volume row is selected it derives the
volume key from the active path's root. It creates a missing `VolumeSetting`,
rejects duplicate rules of the same path and include/exclude polarity, and
uses a new `Exclude rule` checkbox to create either polarity. Existing rules
are displayed but the UI still has no free-form path edit or arbitrary rule
picker; Add Rule uses the current navigation workspace path. This is a
functional ordered rule editor, not proof of full settings UI acceptance.

The rule list is positioned below the status/pause controls and is hidden with
the other Indexing controls when switching pages. No protocol or engine rule
semantics were changed; `IndexPipeline::SetVolumeRules` and the existing
longest-prefix implementation remain protected.

### 48.2 Test-isolation change

`tests/navigation/test_navigation_workspace.cpp` now clears the in-memory
bookmark vector at the start of `TestPerTabPaneContexts`. The test was failing
only because prior UIA runs had legitimately persisted a real user-profile
bookmark under `%LOCALAPPDATA%\\FastFiles\\workspace-state.json`; the test was
asserting exactly one bookmark without isolating persisted state. The change
does not weaken the duplicate-path assertion and keeps persistence coverage in
`TestWorkspacePersistenceFallback`.

The file was normalized back to CRLF after the edit. `ffdoc_drift_tests` now
passes.

### 48.3 Verifier changes

File: `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`.

The verifier now passes the launched process ID through settings, address-bar,
and palette helpers. Settings searches are global but process-constrained,
because `FastFilesSettingsDialog` is a top-level owned `WS_POPUP`, not a UIA
descendant of `FastFilesMainWindow`. Address-bar and palette controls retain a
main-window descendant search and fall back to process-constrained global
search. Palette expected controls use the same fallback. This corrects a
false-negative harness assumption without weakening control assertions.

Fresh wrapper run after this repair:

```text
pwsh -NoProfile -File .\\verify\\verify.ps1 run -Capability windows-ui-automation-validation -SkipAnalyze -SkipTests
status=FAIL reason=driver-exception
```

The run tree is under `verify/runs/ad-hoc/` and is intentionally not read per
repository instructions. The result is therefore **UIA unavailable/driver
failure**, not product proof. The wrapper launched the development UI and
left only its sibling engine process; that exact development engine path was
stopped afterward. No user files were touched. Do not claim settings,
address-bar, or storage UIA acceptance from this run.

### 48.4 Validation after the slice

* `cmake --build build/debug --parallel 1 --target FastFiles`: PASS.
* `cmake --build build/analyze --parallel 1`: PASS, warning-clean.
* Focused `ffnavigation_tests`: PASS.
* Full serial `ctest --test-dir build/debug --output-on-failure -j 1`:
  **36/36 PASS** after normalizing the test file's line endings.
* `git diff --check` on all files from this slice: PASS (Git only warns that
  the working copy's LF will be converted to the repository's CRLF on a future
  Git write; no whitespace errors).

### 48.5 Current truth after the slice

The highest-impact unfinished product seam is still interactive Direct2D row
activation: diagnostics prove the queued/sent/received/snapshot-refresh path
for programmatic/runtime navigation, but the physical mouse hit-test edge is
not deterministically UIA-verifiable because ColumnView rows have no UIA
provider. The next product slice should add a bounded non-destructive
integration seam for synthetic root -> drive -> temporary directory -> child,
with diagnostics and a populated second column as acceptance evidence.

The settings rule editor is now **implemented but runtime-unverified**. It
should be included in the next interactive smoke run if a functioning UIA or
manual desktop session is available. Keep `PinnedSignatures.h`, service
authentication, protocol framing, MFT/USN parser, installer ACLs, and tested
file operations protected.

## 49. Final transfer block for the next agent (latest exact state)

Read this block first if the current agent expires. It is intentionally
operational rather than narrative.

```text
REPOSITORY: D:\Documents\tryPython\FastFiles
BRANCH/HEAD: main / 52d33da0291f7914dbd7deabbd6e70f27ed808e0
GIT SAFETY: dirty work is valuable; nothing is staged; do not reset/restore/
checkout/clean/stash/revert/rebase/merge/cherry-pick/switch/commit/delete.
CURRENT INVENTORY: 109 git status entries at the latest check; 76 tracked
files differ (+3696/-611 aggregate); 79 untracked paths/files. The numbers
include pre-existing agent/tooling sync artifacts. Do not classify by mtime.
HANDOFF FILE: FASTFILES_FRONTIER_HANDOFF.md (this file), sections 47-49 are
newest and supersede older stale claims.
SECURITY: zero Authenticode pins in src/setup/include/ffsetup/PinnedSignatures.h
are intentional fail-closed placeholders. Never bypass/fake/replace pins or
weaken engine<->service authentication, ACLs, protocol bounds, MFT $DATA
exclusion, installer hardening, or constrained LocalSystem broker design.
LAST PRODUCT SLICE: SettingsDialog now displays per-volume ordered rules and
implements add/remove/up/down, including an Exclude rule checkbox. It is
runtime-unverified but debug/analyze clean. No engine/security protocol change.
LAST TEST FIX: navigation test clears loaded bookmarks before testing
case-insensitive duplicate insertion; this isolates user profile state and
does not weaken the assertion.
LAST VERIFIER SLICE: UIA settings/palette/address helpers now process-filter
global fallback searches for owned WS_POPUP/top-level controls. Fresh wrapper
run remains FAIL/driver-exception; UIA is unavailable evidence, not product
failure. Never read verify/runs, verify/baselines, or verify/.signing.
GATES: debug FastFiles build PASS; analyze build PASS; full serial CTest
36/36 PASS; ffdoc_drift_tests PASS; git diff --check PASS on the latest slice.
RUNTIME: development FastFiles.exe + sibling FastFilesEngine.exe launch;
diagnostics repeatedly show ui-directory-request-queued -> sent ->
engine-directory-request-received -> ui-snapshot-column-refresh for nested
temporary paths. Exact WM_LBUTTONDOWN -> Direct2D row activation is not
deterministically UIA-verifiable; do not call it broken solely from old
REQUEST_DIR_COUNT=0 evidence. Exact development processes were stopped after
manual runs; no user files were touched.
NEXT PRODUCT SLICE: add a bounded, non-destructive acceptance seam for
synthetic This-PC root -> drive -> temporary directory -> child. Assert
request lifecycle diagnostics and a populated second column. Prefer a pure or
message-level seam because Direct2D rows have no UIA provider. Then re-run
debug/analyze/full CTest and append a new section here.
SECONDARY P1s: indexed search is functional but reconstructs all projection
paths per request (not WizFile scale); snapshot is fixed 2x8 MiB with raw
header/data-size publication/read risks; privileged MFT/USN path is never
release/runtime validated until secure pins exist; network/UNC/long-path and
full storage/treemap interactive acceptance remain incomplete.
PROTECTED COMPONENTS: protocol/frame parser, EngineClient pipe hardening,
MFT/USN parser/scanner, service ACL/auth, installer, file operations, renderer,
verification harness, existing dirty recovery work.
DO NOT EDIT NOW: OpenSpec task checkboxes, signature pins, security design,
verify generated evidence, broad tooling-sync files. Do not run OpenSpec list
generators; they refresh mtimes.
TOOLCHAIN: import VsDevCmd x64; set FASTFILES_NINJA_EXE to the VS Ninja; use
cmake --build build/debug --parallel 1, ctest --test-dir build/debug
--output-on-failure -j 1, cmake --build build/analyze --parallel 1.
CHECKPOINT POLICY: before any future broad implementation, ask the user or
document a WIP branch/patch/WIP commit proposal; this agent did not mutate Git.
```

### 49.1 Exact source symbols for immediate continuation

Start with `ColumnView::OnMouseDown`, `ColumnView::ActivateItem`,
`ComputeColumnHitTest` (`src/ui/src/ColumnView.cpp/.h`),
`WindowShell::HandleMessage` WM_LBUTTONDOWN (`src/ui/src/WindowShell.cpp`),
`ColumnView::RequestColumn`, `EngineClient::RequestDirectory` and
`UiServer::HandleRequestDirectory`. The existing diagnostic states are emitted
by `EngineClient.cpp`, `UiServer.cpp`, and `ColumnView.cpp`; search for
`ui-directory-request-queued`, `ui-directory-request-sent`,
`engine-directory-request-received`, and `ui-snapshot-column-refresh`.

For the settings slice, use `SettingsDialog::UpdateRuleList`,
`AddVolumeRule`, `RemoveVolumeRule`, `MoveVolumeRuleUp`,
`MoveVolumeRuleDown`, `PopulateIndexingPage`, and `SaveCurrentPage` in
`src/ui/src/SettingsDialog.cpp`. The persisted model is
`ffprotocol::VolumeSetting::rules`; engine semantics are in
`ffprotocol::IsPathIncluded` and `IndexPipeline::SetVolumeRules`. Keep the UI
rule editor additive and do not duplicate rule precedence in the engine.

For verifier topology, use `verify/uia-driver/internal/Elements.ps1`:
`Find-UiaElement -FromElement $main` only walks descendants, while
`Find-UiaElement -ProcessId $launched.Id` searches the UIA root with a process
filter. `FastFilesSettingsDialog` is a top-level owned `WS_POPUP`; the UIA
driver may still be unavailable in the current shell even though Win32
`MainWindowHandle` and class `FastFilesMainWindow` are present.

## 50. Post-validation micro-fix (latest)

`SettingsDialog::UpdateVolumeList` now preserves the selected volume by path
when status refreshes rebuild the Win32 list. Without this, the asynchronous
engine status callback could clear the selected volume immediately after the
user chose it, making the rule list appear empty and making remove/reorder
actions unreliable. The selected rule list is still rebuilt from the canonical
`settings_->indexing` vector; no duplicate state was introduced.

The exact final gate after this micro-fix is:

```text
cmake --build build/debug --parallel 1                         PASS
ctest --test-dir build/debug --output-on-failure -j 1          36/36 PASS
cmake --build build/analyze --parallel 1                       PASS
ffdoc_drift_tests                                               PASS
git diff --check (latest slice)                                 PASS
```

The aggregate dirty-tree count remains intentionally high because the
repository already contains the recovery/search/UI work plus generated agent
tooling sync artifacts. Do not interpret the successful gates as permission
to clean or commit anything.

## 51. Storage sort callback fix (latest)

`src/ui/src/StorageAnalysis.cpp` now calls `SortItems(sortColumn_,
sortAscending_)` after both parent aggregate completion and per-directory
aggregate completion. The owner-data list previously invalidated and repainted
with updated size/percentage values but retained the provisional item order,
which made a selected “largest folders” sort stale until the user clicked a
column header again. The fix is local to callback ordering; it does not alter
aggregate semantics, request IDs, or degraded partial-coverage behavior.

After this fix, the final validation remains:

```text
ctest --test-dir build/debug --output-on-failure -j 1          36/36 PASS
cmake --build build/analyze --parallel 1                       PASS
ffdoc_drift_tests                                               PASS
```

Normalize any newly edited first-party source back to CRLF before running the
drift test; `apply_patch` can insert lone LF into an otherwise CRLF source file.

## 52. Runtime side-effect correction

The manual/UIA development launches did not touch arbitrary user documents or
perform destructive file operations, but the application itself can persist
its normal per-user workspace state and diagnostics while it exits. During
earlier successful UIA smoke runs, `%LOCALAPPDATA%\\FastFiles\\workspace-state.json`
contained a temporary navigation path/bookmark and
`%LOCALAPPDATA%\\FastFiles\\logs\\diagnostics.log` received the expected
request traces. Treat those as application-owned runtime evidence, not as
"no files touched" in the absolute sense. The temporary test directory was
removed by the verifier where its cleanup path ran. The next agent should not
restore or delete the user's settings/logs without explicit authorization;
mention this side effect if reporting runtime validation.

## 53. Final read-only audit appendix for transfer (2026-08-08)

This appendix was added after a fresh static pass. It does not change product
code, OpenSpec tasks, security configuration, or Git state.

### 53.1 OpenSpec inventory observed without status-generating commands

The repository currently contains 17 named directories under
`openspec/changes/` (plus the `archive` directory) and 26 capability
directories under `openspec/specs/`. The task-file checkbox counts at this
checkpoint are:

| Change | Checked | Open | Transfer interpretation |
|---|---:|---:|---|
| `address-engineering-audit-findings` | 0 | 35 | Untracked audit draft; not implementation-ready. It contains malformed checkbox syntax and design assumptions that conflict with the synchronous pipe/security invariants. |
| `autonomous-runtime-verification` | 66 | 2 | Nearly complete; remaining tasks require evidence, not inferred completion. |
| `close-independent-validation-gaps` | 2 | 32 | Current broad open-work change; do not mark tasks from passing unit tests alone. |
| `recovery-degraded-browsing` | 0 | 19 | Under-claims the implementation already present in the dirty tree; runtime acceptance and bookkeeping remain open. |
| `zero-touch-autonomous-engineering` | 46 | 3 | Mostly implemented; remaining tasks need harness/evidence review. |
| all other named changes | all checked | 0 | Treat as historically closed only where code/spec/runtime evidence agrees. |

Do not run `openspec list` or other generators while preserving this WIP:
the earlier investigation observed generated agent-integration mtimes being
refreshed even though content deltas were unchanged. Use direct read-only file
inspection for task/spec reconciliation.

### 53.2 Navigation/path edge review

`ParseNavigationPath` is implemented in
`src/ui/src/NavigationWorkspace.cpp:423-446` (`ParseNavigationPath`). It:

* trims whitespace and balanced surrounding quotes;
* recognizes `This PC` as the synthetic empty path;
* normalizes `/` to `\\` and expands environment variables;
* accepts drive-root and UNC-looking absolute paths;
* rejects `< > | ? *` and uses a 32,768-character `GetFullPathNameW` buffer;
* leaves existence/permission handling to `CommitAddressBarPath`.

`CommitAddressBarPath` at `NavigationWorkspace.cpp:448-475` deliberately
navigates on `ERROR_ACCESS_DENIED`/`ERROR_PRIVILEGE_NOT_HELD` so the degraded
column can show a permission-denied result. This is correct behavior, not a
false “not found” report.

`ParentNavigationPath` at `NavigationWorkspace.cpp:478-515` handles drive
root → synthetic root and treats a UNC server/share root as one navigable
root. The remaining evidence gap is extended-length/UNC presentation rather
than basic acceptance: `Breadcrumbs` (`NavigationWorkspace.cpp:153-188`) and
the parent helper are written for ordinary `C:\\...`/`\\\\server\\share`
forms, so `\\?\\`-prefixed paths should receive a dedicated runtime test
before being called production-grade. This is a P2/P3 compatibility gap,
not the current folder-navigation blocker.

### 53.3 Current static execution boundary

The meaningful degraded browse path is now traceable in code and diagnostics:

```text
WindowShell WM_LBUTTONDOWN
  -> DIP coordinate conversion / details reservation
  -> ColumnView::OnMouseDown / ComputeColumnHitTest
  -> ColumnView::ActivateItem / RequestColumn
  -> EngineClient::RequestDirectory
  -> retained request queue + successful pipe write
  -> UiServer::HandleRequestDirectory
  -> DegradedModeEnumerator
  -> SnapshotPublisher
  -> EngineClient snapshot retry/read
  -> ColumnView refresh
```

Observed diagnostics include `ui-directory-request-queued`,
`ui-directory-request-sent`, `engine-directory-request-received`, and
`ui-snapshot-column-refresh` for temporary nested paths. The one remaining
acceptance seam is deterministic physical hit-testing of a Direct2D row: rows
have no UI Automation provider, and the current UIA driver cannot reliably
discover the main window from the desktop root. Do not downgrade the code path
to “broken” solely because UIA reports `driver-exception`; classify it as
runtime-unverified until a message-level or deterministic acceptance seam is
available.

### 53.4 Fresh-agent priority ordering

1. Preserve this exact dirty tree and the handoff file before any new work.
2. Re-run only the documented read-only inventory and the already-known build,
   CTest, analyze, drift, and diagnostic checks; do not regenerate OpenSpec
   tooling files.
3. Close the physical-row acceptance evidence gap with a bounded,
   non-destructive test seam (synthetic This PC → drive → temporary directory →
   child), without adding a privileged bypass or UIA dependency.
4. Then address one vertical at a time: honest indexed-search scalability,
   snapshot capacity/overflow, storage/treemap acceptance, and only afterward
   privileged MFT/USN release validation with real signed pins.
5. Keep the service protocol, frame parser, authentication, installer ACLs,
   MFT/USN parser, file operations, renderer, and verification harness
   protected until a targeted regression test proves a change is needed.

### 53.5 Transfer command block

```powershell
Set-Location 'D:\Documents\tryPython\FastFiles'
Get-Content .\FASTFILES_FRONTIER_HANDOFF.md -Tail 220
git status --short
git diff --stat
```

The next agent should read this appendix together with sections 46–52; later
sections supersede earlier claims where they conflict.

### 53.6 Process-state caveat at handoff creation

At the final read-only check, no `FastFiles.exe` or `FastFilesEngine.exe`
process was found, but an existing `FastFilesIndexSvc.exe` process (PID 59160,
creation time 2026-08-03) was present. This process was not started, stopped,
or otherwise modified by this continuation. Do not terminate or restart it
automatically; verify ownership and service state before any future runtime
work, and record whether it predates the investigation.

## 54. Treemap direct-click usability slice (2026-08-08 continuation)

### 54.1 Defect and correction

`TreemapView::HandleLButtonDown` previously navigated only when
`hoveredNode_` had already been populated by `WM_MOUSEMOVE`. Opening the
treemap while the pointer was stationary could therefore make the first click
on a visible tile a no-op. This was a real input-state dependency, not merely a
UIA limitation.

The correction is intentionally small:

* `src/ui/include/ffui/TreemapLayout.h` now exposes the pure
  `HitTestClientPoint` seam, combining the shell translation/scale inverse and
  `HitTest`.
* `src/ui/src/TreemapView.cpp:362-397` uses that seam for hover and resolves the
  clicked tile directly from `lParam`, updating `hoveredNode_` before invoking
  the existing navigation callback.
* No command, IPC, security, or aggregate semantics changed.

### 54.2 Evidence

The focused regression test
`tests/ui/test_treemap_layout.cpp:124-151` proves a translated client point
resolves a tile without a preceding hover event and rejects a point outside the
translated viewport. The code and test are now in the build graph.

Validation after this slice:

```text
cmake --build build/debug --target fftreemap_layout_tests --parallel 1  PASS
ctest --test-dir build/debug -R fftreemap_layout_tests --output-on-failure PASS
cmake --build build/debug --parallel 1                              PASS
ctest --test-dir build/debug --output-on-failure -j 1               36/36 PASS
cmake --build build/analyze --parallel 1                            PASS
ffdoc_drift_tests (included in full CTest)                          PASS
```

The direct-click correction is statically and unit-tested; a full interactive
treemap click remains runtime/UIA-unverified because the Direct2D surface has
no automation provider. This supersedes the earlier “treemap click depends on
hover” item; remaining treemap evidence concerns aggregate completeness and
large-tree performance, not the first-click input path.

### 54.3 Next slice

Re-run the same protected-tree inventory, then choose the next independent
vertical. Prefer either (a) a deterministic message-level acceptance seam for
the degraded Column View path, or (b) a bounded indexed-search scalability
improvement with a measured test. Do not combine those with snapshot format,
service authentication, or installer changes.

## 55. Extended-length navigation compatibility slice (2026-08-08 continuation)

### 55.1 Defect and correction

The UI manifest declares `longPathAware`, and the degraded enumerator converts
ordinary paths to extended Win32 paths before `FindFirstFileExW`. However,
`ParseNavigationPath` rejected every `?` character before canonicalization,
which made explicit `\\?\\C:\\...` and `\\?\\UNC\\...` address-bar entries
impossible. It also did not explicitly reject `\\.\\` device namespaces.

`src/ui/src/NavigationWorkspace.cpp:16-32` now accepts only extended drive
roots or extended UNC roots, rejects device namespaces, and
`ParseNavigationPath` (`:434-467`) performs environment expansion before
classifying the prefix. Reserved wildcard characters remain rejected after the
prefix, and trailing separators are preserved for an extended drive root so
`\\?\\C:\\` is not reduced to the invalid `\\?\\C:`.

### 55.2 Regression evidence

`tests/navigation/test_navigation_workspace.cpp:18-23` now covers extended
drive and UNC acceptance, device-path rejection, and wildcard rejection after
the extended prefix. The focused navigation test passed, followed by:

```text
cmake --build build/debug --parallel 1                 PASS
ctest --test-dir build/debug --output-on-failure -j 1  36/36 PASS
cmake --build build/analyze --parallel 1               PASS
ffdoc_drift_tests                                      PASS (full CTest)
```

This closes the address-bar parser defect only. Extended-path breadcrumb
presentation and full long-path runtime traversal still need a dedicated
runtime acceptance test; do not claim complete long-path parity from parser
unit coverage alone.

### 55.3 Breadcrumb/Up completion

The same slice now covers the immediate presentation/state consequence:
`NavigationWorkspace::Breadcrumbs` recognizes extended drive and UNC prefixes,
and `ParentNavigationPath` preserves those prefixes while returning the correct
root-to-synthetic-root transition. Tests cover extended drive breadcrumbs,
extended drive root Up, and extended UNC parent traversal. Full long-path
runtime traversal remains an evidence gap because the current UI automation
harness cannot drive the Direct2D surface reliably.

## 56. Extended-path Column View ancestor-chain completion (2026-08-08 continuation)

`ColumnView::BuildNavigationPathChain` originally recognized ordinary drive and
UNC paths only. Address-bar navigation to an accepted `\\?\\` path therefore
created a single opaque column instead of the Finder-style root/ancestor chain.
The helper in `src/ui/src/ColumnView.h:70-166` now handles extended drive and
extended UNC prefixes while preserving the real path strings sent to the
engine. `tests/ui/test_column_hit_test.cpp:67-73` covers both chains.

Validation after this completion:

```text
cmake --build build/debug --target ffcolumn_hit_test_tests --parallel 1 PASS
ctest --test-dir build/debug -R ffcolumn_hit_test_tests --output-on-failure PASS
cmake --build build/debug --parallel 1                              PASS
ctest --test-dir build/debug --output-on-failure -j 1               36/36 PASS
cmake --build build/analyze --parallel 1                            PASS
ffdoc_drift_tests (included in full CTest)                          PASS
```

The parser, breadcrumb, parent, and ancestor-chain seams are now covered;
actual >260-character filesystem traversal still requires a safe temporary
directory runtime test and remains unverified.

## 57. Latest exact transfer checkpoint (2026-08-08 continuation)

Current read-only Git inventory is branch `main`, HEAD `52d33da`, staged files
`0`, modified entries `78`, and untracked entries `79`. The increase from
the earlier 77-modified baseline is product work in the same dirty tree:
treemap direct-click handling, extended-path navigation, and their tests; no
prior changes were reset, cleaned, staged, or committed.

Latest validation is green: debug build, serial CTest `36/36`, analyze build,
and `ffdoc_drift_tests` (as part of CTest). The only FastFiles-named process
observed after validation is the pre-existing `FastFilesIndexSvc.exe` PID
59160; this continuation did not start, stop, or mutate it.

The next agent should treat the objective as active. Remaining high-impact
work includes deterministic runtime acceptance of physical Direct2D browsing,
indexed-search scale/cancellation, fixed snapshot capacity, complete storage
and treemap aggregate acceptance, and secure privileged MFT/USN operational
validation once real signed pins are provisioned. None of those are proven by
the 36 passing tests alone.

## 58. Session-transfer manifest (final read-only checkpoint)

This section is intentionally the last word for a new session. If an earlier
section disagrees with this one, prefer this section for repository state and
prefer the later per-slice sections for implementation details. The handoff
file itself is untracked and valuable; preserve it together with the rest of
the dirty tree.

### 58.1 Exact state to establish before doing anything else

Run only read-only commands first:

```powershell
git status --short --untracked-files=all
git diff --stat
git diff --cached --stat
git branch --show-current
git rev-parse HEAD
```

The state recorded at this checkpoint is:

```text
branch             main
HEAD               52d33da0291f7914dbd7deabbd6e70f27ed808e0
staged files       0
modified entries   78
untracked files    79
tracked diff stat  77 files, 3905 insertions, 619 deletions
```

The apparent difference between “78 modified entries” and the 77-file diff
stat is a status/diff accounting distinction, not evidence that a file should
be removed. Do not normalize it by cleaning, restoring, staging, or committing.

### 58.2 What this session actually did

No Git mutation was performed: no reset, restore, checkout, clean, stash,
revert, rebase, merge, cherry-pick, switch, or commit. No service was started
or stopped. The only product-code work in this continuation was bounded UI and
navigation compatibility work:

* Treemap direct clicks now resolve from the click point instead of depending
  on a prior mouse-move event (`TreemapLayout::HitTestClientPoint`,
  `TreemapView::HandleLButtonDown`).
* Address parsing, breadcrumbs, parent navigation, and Column View ancestor
  chains now preserve accepted extended drive/UNC paths (`\\?\\C:\\...`,
  `\\?\\UNC\\server\\share\\...`) and reject device namespaces/wildcards.
* Focused tests cover those seams; no claim is made that a real >260-character
  directory has been traversed through the Direct2D surface.
* The handoff document was extended only with evidence and transfer guidance.

### 58.3 Evidence boundary

The strongest reproducible gate is:

```text
Debug build                         PASS
Serial CTest                        36/36 PASS
Analyze build                       PASS, warning-clean
ffdoc_drift_tests                   PASS (inside full CTest)
Focused treemap layout test         PASS
Focused navigation test             PASS
Focused Column View hit test        PASS
```

Static and message-level runtime evidence proves engine discovery, UI-pipe
connection, subscribe, directory-request receipt, degraded enumeration, and
snapshot refresh for programmatically driven paths. It does not prove the
physical Direct2D row hit from `WM_LBUTTONDOWN` through a real human click.
The UIA wrapper currently fails with `driver-exception`/cannot reliably expose
the custom-rendered rows, so do not relabel that harness failure as a product
pass or as a product defect without a separate controlled repro.

The only FastFiles-named process observed after validation was the pre-existing
`FastFilesIndexSvc.exe` PID 59160. Treat it as external state; do not kill it,
attach to it, or infer that the current UI/engine pair was runtime-tested from
its presence.

### 58.4 First continuation decision

The next engineer must choose one small, evidence-producing vertical and keep
the rest read-only. Recommended order:

1. Add a deterministic message-level acceptance seam for physical Column View
   activation (or a safe, temporary test-only input injection) and prove the
   exact first failing edge; do not add a UIA provider as a speculative rewrite.
2. Finish storage/treemap aggregate acceptance, including directory sizes and
   owner-data interaction, then test a bounded real tree.
3. Reduce indexed-search cancellation/scale risk; the current path still
   reconstructs projection paths under a coarse mutex and is not WizFile-scale.
4. Measure the fixed 2x8 MiB snapshot cliff and define an explicit overflow or
   fallback contract before attempting larger-volume claims.
5. Keep privileged MFT/USN operational validation blocked on real approved
   signatures; never weaken the fail-closed authentication gate.

For any implementation slice, create or update the appropriate OpenSpec change
before code work, keep it narrow, add a meaningful test, run the focused test,
then the serial 36-test suite and analyze build. Do not mark tasks complete on
static existence alone.

### 58.5 Protected evidence and files

Preserve all dirty product and tooling content, especially:

```text
FASTFILES_FRONTIER_HANDOFF.md
openspec/changes/recovery-degraded-browsing/
openspec/changes/address-engineering-audit-findings/
src/ui/src/EngineDiscovery.{h,cpp}
tests/ui/test_engine_discovery.cpp
src/setup/include/ffsetup/PinnedSignatures.h
src/setup/src/AuthenticodeVerification.cpp
src/engine/src/{SnapshotPublisher,DirectoryWatcher,UiServer,IndexPipeline}.*
src/ui/src/{EngineClient,ColumnView,NavigationWorkspace,StorageAnalysis,TreemapView,WindowShell}.*
```

The all-zero signature pins are an intentional fail-closed placeholder and
must remain untouched. The audit-draft change may contain unsafe or stale
assumptions (including authentication bypass ideas and incorrect overlapped
pipe assumptions); treat it as evidence to reconcile, never as an instruction
to apply blindly. Do not read the forbidden `verify/runs`, `verify/baselines`,
or `verify/.signing` trees.

### 58.6 Safe handoff commands

The incoming session may use these non-mutating commands to confirm the handoff:

```powershell
Get-Item .\FASTFILES_FRONTIER_HANDOFF.md | Select FullName,Length,LastWriteTime
Get-Content .\FASTFILES_FRONTIER_HANDOFF.md -Tail 120
git status --short --untracked-files=all
git diff --check
cmake --build build/debug --parallel 1
ctest --test-dir build/debug --output-on-failure -j 1
cmake --build build/analyze --parallel 1
```

If a checkpoint is needed before implementation, the proposed (not executed)
preservation choices are a WIP branch, a WIP commit, a patch export, or a
working-tree copy. Select one only with explicit user authorization and record
the exact file set; never improvise a destructive “cleanup.”

### 58.7 One-screen transfer summary

```text
Current product state: usable degraded browse/search shell, not production file manager.
Immediate runtime question: physical Direct2D row click -> RequestDirectory edge.
Confirmed gates: 36/36 tests, debug build, analyze build, drift test.
Largest unproven systems: physical input, long-path runtime, storage aggregate UX,
snapshot scale, indexed-search scale, signed privileged MFT/USN operation.
Security rule: preserve closed protocol, ACLs, mutual auth, path/signature checks,
and all-zero fail-closed pins.
Next agent: read this file through §58, inspect status read-only, choose one bounded
vertical, and leave all unrelated dirty work untouched.
```

## 59. Indexed nested-path correctness slice (2026-08-09 continuation)

### 59.1 Defect found

Static re-audit found that both persistent-index consumers dropped the first
real ancestor component for nested paths. `Projection::ReconstructPath` returns
the root-to-entry sequence; with an empty NTFS root name, a parent such as
`Users\\Alice` has no synthetic leading separator. The previous “strip through
the first separator” logic therefore published or returned `C:\\Alice` instead
of `C:\\Users\\Alice`. This was not caught by the existing one-level tests.

### 59.2 Product fix

`src/engine/src/IndexPipeline.cpp:51-72` now centralizes root-name removal in
`StripVolumeRootName`. It walks the bounded parent chain, removes only the exact
root record name (including a non-empty name such as `.`), and preserves every
descendant component. The helper is used by:

* `IndexPipeline::ExportDirectorySnapshot` (`:422-424`), which feeds indexed
  directory listing/snapshot publication;
* `IndexPipeline::SearchIndexed` (`:547-554`), which builds result folders for
  the persistent search response.

This is a correctness fix, not a security-boundary change. It does not alter
the protocol, service authentication, snapshot capacity, or MFT/USN gates.

### 59.3 Regression coverage

`tests/engine/test_index_pipeline.cpp:44-66` now includes `Users\\Alice\\nested.txt`
and asserts that the exported key is exactly `C:\\Users\\Alice`. The indexed
search fixture at `:201-227` uses a non-empty synthetic root name (`.`), checks
the direct result folder `C:\\Users`, and checks the nested result folder
`C:\\Users\\Alice`. The test uses name lookup rather than assuming projection
child insertion order.

### 59.4 Validation evidence

```text
ffengine_index_pipeline_tests       PASS
serial CTest                        36/36 PASS
debug build                         PASS
analyze build                       PASS, warning-clean
ffdoc_drift_tests                   PASS (inside serial CTest)
git diff --check                    PASS for the new source/test hunks
```

The source and test files were normalized back to CRLF after patching; no
generated product file was regenerated and no Git mutation was performed.

### 59.5 Remaining interpretation

This closes a real persistent-index path correctness bug, but it does not prove
that the privileged service is active or that a large live NTFS index can be
published within the fixed 2x8 MiB snapshot slots. Runtime search acceptance
still needs a signed-service or controlled seeded-index scenario; degraded-mode
search remains the safe active path. The next agent should preserve this fix
and continue with one bounded vertical, preferably deterministic physical
Column View input acceptance or storage/treemap aggregate acceptance.

## 60. Storage file-activation routing slice (2026-08-09 continuation)

### 60.1 Defect found

Storage Analysis list activation and treemap activation previously shared a
path-only callback. `StorageAnalysis::HandleNotify` (`src/ui/src/StorageAnalysis.cpp`)
sent a file path through the same callback used for folders, and
`TreemapView::HandleLButtonDown` did likewise. `WindowShell` then called
`NavigationWorkspace::Navigate` and `ColumnView::NavigateToPath` for that file.
That attempted to materialize a file as a directory column instead of opening
the file through its registered shell association. The ordinary Column View
`item.open` command already had the correct shell behavior, so this was a
disconnected UI integration defect rather than a shell/file-operation rewrite.

### 60.2 Product fix

`src/ui/src/TreemapView.h:33-60` now defines a typed `NavigateHandler` carrying
`(path, isDirectory)`. `TreemapView::HandleLButtonDown` forwards the selected
node's type (`TreemapView.cpp:393`). `StorageAnalysis` propagates the same type
through its initialization callback and list activation:

* directory tiles/rows update the storage path and continue to drill/navigate;
* file tiles/rows are reported as `isDirectory == false` and do not mutate
  navigation state.

`WindowShell::Initialize` (`src/ui/src/WindowShell.cpp:695-706`) now routes a
false type through the existing `OpenWithDefaultApplication` helper and shows
the same explicit error message used by `item.open` if the shell rejects it.
The directory path remains on the previous navigation path. No privileged,
IPC, protocol, or file-operation engine code was changed.

### 60.3 Validation

```text
Debug build                         PASS
Serial CTest                        36/36 PASS
Analyze build                       PASS, warning-clean
ffdoc_drift_tests                   PASS (inside serial CTest)
git diff --check                    PASS for the changed hunks
```

There is no live shell-association E2E test in the current suite; the evidence
boundary is compile/link coverage plus the existing `OpenWithDefaultApplication`
implementation. A future runtime acceptance should use a harmless temporary
text file and confirm that a storage/treemap activation launches its associated
handler without changing the browser path.

### 60.4 Handoff consequence

Storage file activation is no longer structurally disconnected, but aggregate
completeness, owner-data interaction, and physical Direct2D click delivery are
still unverified at runtime. Do not call the storage analyzer production-ready
from this slice alone. Keep the next change bounded and preserve the typed
callback contract.

## 61. Safe message-level runtime acceptance (2026-08-09 continuation)

An isolated runtime probe launched the current debug `FastFiles.exe` with a
temporary `LOCALAPPDATA`, allowed the discovered engine to connect, and sent a
synthetic `WM_LBUTTONDOWN`/`WM_LBUTTONUP` to the first drive-row coordinate.
The client window was 804x454; the probe clicked the `C:\\` row at client
coordinates `(240,126)`. The temporary tree contained no user data and the
probe terminated only the two processes it started; the pre-existing
`FastFilesIndexSvc.exe` PID 59160 was left untouched.

The isolated diagnostics log recorded this exact ordered chain:

```text
engine-launched
engine-connect-succeeded
ui-directory-request-queued       path=C:\\
ui-directory-request-sent         path=C:\\
engine-directory-request-received path=C:\\
ui-snapshot-column-refresh        path=C:\\ items=35
```

This is strong runtime evidence for the message-level edge:

```text
WM_LBUTTONDOWN -> ColumnView::OnMouseDown -> ActivateItem
 -> RequestColumn -> EngineClient::RequestDirectory
 -> UI pipe -> UiServer -> degraded enumeration -> snapshot refresh
```

It closes the earlier “no directory request observed” uncertainty for a
synthetic drive-row message. It does **not** prove a physical human click,
because the Direct2D surface has no UIA provider and the existing wrapper
cannot reliably expose custom-rendered rows. It also does not prove navigation
of every drive type or a >260-character path. Keep those as explicit runtime
acceptance gaps rather than converting this message-level pass into a blanket
UI pass.

## 62. Search fallback path-root correction (2026-08-09 continuation)

The next bounded usability slice corrected a real path-model defect in the
degraded/in-process search fallback. `SearchPanel::PopulateCandidates`
previously implemented parent lookup with a raw `find_last_of` and stripped
the separator from the parent. Consequently a child of `C:\\` was assigned
folder `C:` rather than `C:\\`; extended drive paths were assigned
`\\\\?\\C:`; and UNC share roots were not preserved. Current-folder and
current-folder-plus-subfolders searches could therefore silently omit browsed
entries even though the snapshot contained them. The same local code also
used a path-prefix hash that did not distinguish extended drive/UNC volumes.

### 62.1 Product change

Added the small shared search-path seam:

* `src/search/include/ffsearch/Path.h`
* `src/search/src/Path.cpp`
* `src/search/CMakeLists.txt`

The seam provides `ffsearch::ParentPath`, `LeafName`, `SameVolume`, and
`VolumeId`. It recognizes normal drives, extended drives, normal UNC shares,
and extended UNC shares; preserves the exact root spelling returned by the
snapshot; treats the share root as the UNC volume root; and compares volume
scope case-insensitively. `src/search/src/Search.cpp` now uses `SameVolume`
for `CurrentDrive`. `src/ui/src/SearchPanel.cpp` now uses the shared helpers
for candidate parent/name/volume construction, eliminating the duplicate
naïve path logic. No protocol, engine, privileged service, file-operation, or
security invariant changed.

### 62.2 Focused evidence

`tests/search/test_search.cpp` now checks:

```text
C:\\Users                       -> C:\\
\\\\?\\C:\\Users               -> \\\\?\\C:\\
\\\\server\\share\\folder      -> \\\\server\\share
\\\\?\\UNC\\server\\share\\folder -> \\\\?\\UNC\\server\\share
normal/extended drive equivalence for CurrentDrive
extended-drive filtering excludes D:\\
```

The focused `ffsearch_scan_tests` executable passed all checks.

### 62.3 Gates

```text
Debug build                         PASS
Serial CTest                        36/36 PASS
Analyze build                       PASS, warning-clean
ffdoc_drift_tests                   PASS (inside serial CTest)
git diff --check                    PASS for changed hunks
```

The fix is structurally verified and test-covered, but a live search against
a long-path directory remains unverified because the UIA harness cannot expose
custom Direct2D rows and no durable-index service is active with the
placeholder signature pins. Keep persistent-index scale and end-to-end search
latency as separate follow-up work.

### 62.4 Transfer instruction

The next agent should not re-open the old local `ParentPath` implementation or
revert this seam. Start from `ffsearch::Path` and preserve its tests. The next
high-value slice is either (a) safe runtime acceptance for storage/treemap file
activation with a temporary text file, or (b) snapshot-capacity/overflow
behavior; do not combine those with a broad search rewrite.

## 63. Exact transfer checkpoint after §62

The current tree is still intentionally unsaved and unstaged:

```text
branch                 main
HEAD                   52d33da0291f7914dbd7deabbd6e70f27ed808e0
status porcelain lines 160
modified/other entries 79
untracked entries      81
staged entries         0
tracked diff           78 files, 4019 insertions, 664 deletions
```

The count includes pre-existing tooling/documentation synchronization, the
recovery implementation, test additions, diagnostic artifacts, and the new
search-path seam. These counts are a transfer snapshot, not permission to
stage, commit, reset, or clean anything. The next session must refresh them
read-only before making assumptions.

Current validation immediately before handoff:

```text
build/debug full build                  PASS
build/debug CTest serial                36/36 PASS
build/analyze full build                PASS, /W4 /WX + /analyze clean
ffsearch_scan_tests                     PASS (includes §62 path tests)
ffdoc_drift_tests                       PASS
git diff --check                        PASS for changed hunks
```

Safe continuation command sequence (read/build/test only):

```powershell
git status --short --branch
git diff --stat
git diff --cached --stat
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/debug --parallel 1'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build/debug --output-on-failure -j 1'
```

Do not run `verify` commands that read `verify/runs`, `verify/baselines`, or
`verify/.signing`; those directories are explicitly protected by AGENTS.md.
Do not update OpenSpec checkboxes merely because §62 tests pass. The recovery
change remains an underclaimed runtime work item, and the all-zero signature
pins remain an intentional fail-closed release blocker.

## 64. Snapshot-capacity remediation (2026-08-09 continuation)

The next confirmed usability ceiling was the shared snapshot's old fixed
2×8 MiB capacity. A single large directory or a long sequence of browsed
roots could make `SnapshotPublisher::Publish` return zero; `UiServer` then
surfaced `DirectoryErrorReason::PublishFailed`, correctly avoiding stale data
but leaving the requested listing unavailable. The old capacity was a real
bounded cliff, not merely a theoretical performance concern.

### 64.1 Product change

`src/protocol/include/ffprotocol/SnapshotFormat.h:24` now sets
`kSnapshotSlotCapacityBytes` to 64 MiB per slot. The mapping remains the same
private, double-buffered, zero-IPC format; only the bounded slot size changes.
`SnapshotPublisher::Start` (`src/engine/src/SnapshotPublisher.cpp:37`) derives
the mapping size from that constant, and `Publish` (`:84`) still rejects any
serialized image above the explicit bound. The UI's publish-failure surfacing
is preserved; authentication, IPC, parser limits, and the privileged service
boundary are untouched. The resulting mapping reserves 128 MiB plus the
header address space for the two slots; pages remain demand-committed by the
Windows mapping rather than eagerly materialized by the process.

This is a bounded capacity remediation, not an unlimited-index claim. A
future scale change may need snapshot eviction/chunking or a different
projection seam; do not describe 64 MiB as proof of WizFile-scale indexing.

### 64.2 Regression evidence

`tests/engine/test_snapshot_publisher.cpp:24-35` creates 120,000 entries with
long names. Its serialized size is asserted to be above the old 8 MiB cliff
and below the new 64 MiB bound (`:94-97`), then `Publish` is asserted to
advance to generation three (`:98`). The active slot remains parseable from
the read-only mapped view. The focused publisher test passes.

### 64.3 Gates after the change

```text
Debug full build                     PASS
Serial CTest                         36/36 PASS
ffengine_snapshot_publisher_tests   PASS (large-directory regression)
Analyze full build                  PASS, warning-clean
git diff --check                     PASS for changed hunks
```

The analyze build needed a longer command timeout (the first 120-second
wrapper expired without compiler output; the same read-only build completed
successfully in roughly 101 seconds on the rerun). This was tool timeout, not
a source failure.

### 64.4 Scroll investigation correction

The stale audit statement “Column View has no vertical scrolling” is no
longer current. `ColumnView::ScrollVertical` (`src/ui/src/ColumnView.cpp:352`)
clamps per-pane offsets, rendering subtracts them (`:874`), hit-testing uses
the vertical offset, `WindowShell::WM_MOUSEWHEEL` routes normal wheel input
(`src/ui/src/WindowShell.cpp:1659-1670`), and keyboard Up/Down calls
`EnsureFocusedItemVisible` (`:1750`). No scroll implementation was changed in
§64; any future scroll work should target only a newly reproduced edge (for
example, wheel-to-pane selection or offset clamping after asynchronous row
shrink), not reimplement the existing path.

### 64.5 Next exact slice

The next agent should choose one bounded, evidence-backed target: safe runtime
storage/treemap file activation using a temporary harmless text file, or a
new snapshot retention/eviction design for trees that exceed 64 MiB. Preserve
the explicit publish-failure path and do not increase the slot repeatedly
without measuring memory and serialization cost.

## 65. Storage Analysis Up path unification (2026-08-09 continuation)

`StorageAnalysis::HandleOwnerCommand` previously computed its Up destination
with a local `find_last_of` and a special case for `C:`. That duplicated path
semantics already fixed and tested in `ffsearch::Path`, and could mishandle
extended-length/UNC drilldown locations. `src/ui/src/StorageAnalysis.cpp`
now includes `ffsearch/Path.h` and routes the Up action through
`ffsearch::ParentPath`. The existing root behavior remains deliberate: a
drive/share root returns an empty parent and therefore stays at the storage
root; a deeper location preserves its exact namespace spelling and navigates
to the immediate parent. No storage aggregation, shell, protocol, or security
logic changed.

The shared path regression checks in `tests/search/test_search.cpp` cover
normal drive, extended drive, normal UNC, and extended UNC parent roots, so
the storage handler now consumes a tested seam rather than an untested local
algorithm. The changed UI target and focused search test passed.

Final gates for the combined §64–§65 checkpoint:

```text
Debug full build                     PASS
Serial CTest                         36/36 PASS
Analyze full build                  PASS, warning-clean
ffengine_snapshot_publisher_tests   PASS
ffsearch_scan_tests                  PASS
ffdoc_drift_tests                    PASS
git diff --check                     PASS for changed hunks
```

The working tree remains uncommitted and unstaged. Do not mark OpenSpec tasks
complete solely from these gates; formal recovery/runtime acceptance and the
external signed-service gates remain open.

## 66. Command-palette owner-message routing (2026-08-09 continuation)

Static inspection found that `CommandPalette` already implemented themed
owner-draw and control-color handlers (`src/ui/src/CommandPalette.cpp:183-240`),
but `WindowShell::WndProc` only forwarded `WM_DRAWITEM` to `SearchPanel` and
had no `WM_CTLCOLOREDIT`/`WM_CTLCOLORLISTBOX` cases. The palette controls are
children of the shell, so those messages are delivered to the shell owner;
without forwarding, the owner-drawn list could fall back to default painting
and the edit/list controls ignored the palette theme brushes. This was a
connectedness defect, not a missing palette command or registry entry.

`src/ui/src/WindowShell.cpp` now forwards `WM_DRAWITEM` to
`commandPalette_.HandleDrawItem` before the search-panel handler and routes
`WM_CTLCOLOREDIT` and `WM_CTLCOLORLISTBOX` through
`commandPalette_.HandleCtlColor`, returning the palette brush only when the
message targets one of its controls. Unrelated controls still use
`DefWindowProcW`; the search-panel owner-draw path remains intact. No command
registry, keyboard routing, security, IPC, or persistence behavior changed.

Validation:

```text
Debug incremental build (WindowShell.cpp) PASS
Serial CTest                         36/36 PASS
ffdoc_drift_tests                    PASS
Analyze incremental build           PASS, warning-clean
```

The fix is statically confirmed and build/test verified; a physical palette
interaction was not UIA-verified because the existing UIA wrapper still exits
with `FAIL reason=driver-exception` in this environment. Do not claim the
palette is end-to-end runtime verified until a real interactive or repaired
UIA run demonstrates opening, filtering, owner-draw rendering, and Enter
execution.

The next exact slice remains either (a) a safe runtime storage/treemap file
activation check with a temporary harmless text file, or (b) a measured design
for snapshot retention/eviction above the bounded 64 MiB slot. The all-zero
signature pins and formal OpenSpec checkboxes remain untouched.
