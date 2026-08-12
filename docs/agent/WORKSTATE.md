# Fast Files — Agent Workstate

Last updated: 2026-08-12T23:59:00+08:00
Status: LOCAL HARDENING CAMPAIGN COMPLETE — LOCAL ENGINEERING EXHAUSTED; EXTERNAL RELEASE BLOCKERS PRESERVED

## Active frontier — main UI geometry hardening (2026-08-12)

- This session temporarily supersedes the prior final-validation frontier with
  one bounded product slice: establish and harden the main-shell geometry
  authority. The prior signed/elevated/clean-host/aggregate-backed external
  blockers remain recorded below and are not being erased or reopened.
- Repository reconciliation for this completed slice is historical: the
  geometry implementation is committed as `c4868e5`; the current checkout is
  `main` at the later overnight checkpoint recorded below. Older
  recovery-branch/hash statements in this file remain historical and
  Git/source are authoritative.
- Final source ownership map: `WindowShell::HandleMessage(WM_SIZE/WM_DPICHANGED)`
  drives native repositioning; `WindowShell::CurrentShellLayout()` calls
  `ffui::ComputeMainShellLayout`, whose scene-local DetailsLayout is translated
  to global client-space rectangles. `WindowShell::Render()` applies the same
  content origin to Direct2D ColumnView/Treemap and the mouse/rename paths;
  ColumnView rendering and hit-testing consume the same details-adjusted width.
- Root cause is now proven in the renderer/child-window seam: the main HWND
  lacked `WS_CLIPCHILDREN`, while the DirectComposition target was created with
  `topmost=TRUE`. The native Navigation Chrome, Navigation Sidebar, Search,
  and Storage HWNDs had correct rectangles but were covered by the opaque
  composition visual, producing the apparent upper-left dead region and
  inconsistent edges. A CLI capture with `topmost=TRUE` remained blank even
  after clipping; the final pair `WS_CLIPCHILDREN` + `topmost=FALSE` exposed
  the child surfaces in their measured rectangles.
- Baseline Debug geometry at 96 DPI: client `1008x581`; chrome
  `{0,0,1008,72}`; sidebar `{0,72,220,581}`; post-navigation content
  `{220,72,1008,581}`; default visible DetailsLayout scene width `788`,
  column viewport `{220,72,708,581}`, details pane `{708,72,1008,581}`.
  The wrong visual layer hid the first 220-DIP sidebar region; it did not
  represent an intentional extra reservation.
- The authoritative seam is `ffui::ComputeMainShellLayout` in
  `src/ui/include/ffui/ShellLayout.h`. WindowShell rendering, hit-testing,
  rename placement, resize/DPI repositioning, Search, and Storage consume its
  content/details result. `SearchLayout.h` and `StorageLayout.h` add bounded
  alternate-surface control arrangements; `tests/ui/test_shell_layout.cpp`
  covers shell, Search, Storage, details, dual-pane, resize, short/narrow,
  and 96/120/144/192-DPI DIP invariants.
- CLI evidence: pre-fix child enumeration measured Search query/list at
  `{12,72,...}` / `{12,132,...}` over the sidebar; final enumeration measures
  Search at `{232,72,...}` / `{232,132,996,569}` and Storage at the same
  post-sidebar origin. Final clipped-shell capture visibly shows Navigation
  Chrome, Sidebar, and native controls; the topmost-target isolation capture
  hides them again.
- Previous implementation frontier: the bounded shell-geometry diff was
  reviewed and committed as `c4868e5`; no evidence-backed geometry issue
  remains. Do not reopen that slice without new evidence.
- Validation complete: Debug, Analyze, and Release builds passed; Debug and
  Release serial CTest passed `48/48`; `verify.ps1 doctor` and
  `verify.ps1 build` passed; strict OpenSpec validation passed `45/45`.
  Final CLI runtime launched rebuilt Debug, measured the base/Search/Storage
  HWNDs, exercised Storage open/close, and shut down with no UI/Engine orphan;
  the persistent service PID was left untouched. Final CLI captures are
  `C:\Users\Michael Roy\AppData\Local\Temp\fastfiles-final-base.png` and
  `fastfiles-final-search.png`.
- Final CLI stress acceptance additionally resized through
  `1500x900 -> 1000x700 -> 700x700 -> 1600x900`, verified Search at each
  resized state (including the compact 700-pixel fallback), then drove the
  existing command palette with held native key events through Details hidden,
  Details restored, Dual Pane enabled, and Storage Analysis. The final run
  closed normally; no new UI/Engine process remained and service PID `21148`
  remained running. Earlier failed driver attempts were harness-only
  focus/PowerShell binding mistakes and did not alter source or product state.
- Directly related shell fixes included: shared shell rectangle authority;
  sidebar/chrome-aware Search and Storage positioning; bounded narrow Search
  and Storage arrangements; WM_DPICHANGED child-surface repositioning; and
  suppression of stale browse Details/status painting under alternate surfaces.
  No unrelated subsystem was changed. Product commit: `c4868e5`
  (`Fix main shell content geometry`).

## Overnight local production-hardening campaign (2026-08-12)

- The preceding main-shell geometry campaign is COMPLETE. Its root cause,
  implementation, deterministic tests, CLI runtime evidence, build gates,
  verifier, OpenSpec validation, and commit are recorded above.
- This campaign is now the single active frontier. It must remain CLI-only,
  preserve the known signed/elevated/clean-host/visual external blockers, and
  use one evidence-backed product slice at a time.
- Starting campaign HEAD: `c4868e5`; starting worktree was clean after the
  geometry commit. No campaign-created FastFiles UI/Engine process remains;
  the existing service PID `21148` was not modified.
- First triage method: inspect existing tests and source ownership for
  Priority A/B defects, then run focused deterministic/runtime probes before
  changing code. Do not reopen completed search optimization, shortcut
  persistence/routing, selection identity, Storage category filtering,
  authentication/signing, MFT/USN, installer/service, clean-host, or direct
  visual work without new evidence.

## Overnight campaign NEXT EXACT ACTION

Slice 1 is committed as `a8b236d` (`Reserve duplicate transfer destinations`).
Slice 2 is committed as `9b3828d` (`Refresh Details folder aggregates after state
changes`). Slice 3 is committed as `e5352d1` (`Make directory watch keys case
insensitive`). Slice 4 is committed as `4cb44e3` (`Atomically save workspace
state`). Slice 5 is committed as `db7adf3` (`Bound restored recently closed
tabs`). The bounded Priority B/C lifecycle frontier is now reviewed: the
diagnostic UI launched the exact sibling Engine, remained responsive through an
Engine termination/relaunch, and closed via WM_CLOSE. The Engine remaining after
UI close is intentional: `src/engine/src/Main.cpp` documents a per-logon
background helper that parks until logon-session end, while `WindowShell::Run`
stops only its UI-side `EngineClient`. No duplicate-launch, stale-state, or
product cleanup defect was established. The first probe had a path-normalization
harness bug (`Win32_Process.ExecutablePath` used a `\\?\\` prefix); the exact
diagnostic Engine it left was immediately terminated after path validation.

Slice 6 is committed as `26918d6` (`Unify Windows snapshot path identity`).
Windows path identity is now shared through `ffprotocol::WindowsPathEqual` and
`CaseInsensitivePathLess`. Engine snapshot ownership uses a case-insensitive
ordered map and a matching `SnapshotPublisher` overload without copying the
large map; the watcher reuses the same comparator. UI column refresh, directory
errors, loading markers, and snapshot subtree aggregation use exact lookup with
an ordinal-insensitive fallback. The first stored path spelling remains the
display key. Focused publisher, aggregate, watcher, protocol, and fuzz tests
pass. Slice 7 is implemented in the working tree: the callback gate is shared
only by `UiServer`, listener callbacks register before touching the server, late
callbacks close their pipe after the stop mark, and `UiServer::Stop` cancels
subscribed clients, stops accepting, waits for registered callbacks, then tears
down watcher/snapshot/client state. The lifecycle test passed 10/10 direct CLI
stress runs after the fix and the focused watcher/lifecycle/publisher/aggregate
CTest batch passed 4/4. Slice 7 is committed as `7b9f8fa` (`Prevent UI server
callback teardown race`). Slice 8 is committed as `66bffd0` (`Preserve Unicode
workspace state`). Slice 9 is committed as `273dd2d` (`Prevent in-batch transfer
overwrites`). Slice 10 is committed as `7edef8c` (`Preserve pending selection
across path casing`). Slice 11 is committed as `2c30a7c` (`Use Windows identity
for transfer reservations`). The complete Debug CTest tree now passes 49/49,
including `ffdoc_drift_tests` after the explicit mixed-line-ending cleanup of
18 previously touched first-party files; no semantic source diff resulted from
that normalization. The existing Release large-index benchmark target built
successfully, but the correctly quoted 10-minute run of the default 10K/100K/
500K/1M workload did not finish or emit its end-of-run JSON before the bounded
shell timeout. The exact benchmark PID `17624` was verified by executable path,
stopped, and left no benchmark process or fixture behind. The earlier failed
attempt was only a PowerShell argument-splitting mistake; after the corrected
run timed out, this method is considered inconclusive rather than a product
regression. Do not repeat the same full benchmark method; retain the earlier
completed Release benchmark artifact/results as the authoritative performance
baseline and choose another evidence-backed local hardening frontier.

Slice 12 is committed as `3f3691a` (`Bound and atomically persist search
history`). `SearchPanel` now applies the protocol's 4095 UTF-16-character limit
to the native edit and reads the complete bounded value instead of truncating
at 1023 characters. `SearchHistory` rejects overlong records and persisted
entries, bounds raw history lines, rejects malformed UTF-8, and writes through
a temporary file with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`.
Focused Debug search/protocol/drift tests pass, the full Debug CTest tree passes
49/49, and the focused Analyze build of `FastFiles` plus `ffsearch_scan_tests`
passes cleanly. No signing/authentication or production pin behavior changed.

The documented constant-time thumbprint comparison is already complete in the
current source: `VerifyPinnedSignature` calls
`ConstantTimeThumbprintEqual`, and the focused authenticode tests cover equal,
different, placeholder, and partially populated pins. No authentication change
was made by this campaign.

NEXT EXACT ACTION: inspect the deferred explicit USN journal cleanup finding
(`INFO-20260804-K7P2VN-002`) in the engine/service-connection ownership path.
Only implement a cleanup fix if the current shutdown path leaves a concrete
open journal/session resource or lacks a safe idempotent close; preserve the
closed command protocol and fail-closed authentication.

Slice 13 is committed as `30c76c1` (`Close engine-owned USN sessions on
teardown`). The audit proved that the service-side journal worker already closes
its raw volume handle and joins on Close/disconnect, but the engine erased
disappeared/disabled sessions without sending their connection-scoped Stop and
Close requests, and shutdown did not drain owned sessions before stopping the
privileged connection. `VolumeSessionManager` now tracks whether an Open was
actually requested, serializes Open versus teardown requests, sends Stop then
Close only for owned journal requests, drains sessions before connection stop,
and removes the old invalid Close-on-disable behavior. Focused lifecycle tests
cover disappearance, disable-before-ack, and shutdown ordering; the test passed
20 consecutive direct runs, full Debug CTest passed 49/49, and Debug/Analyze
engine/session-manager builds passed. No service command, authentication, or
raw-volume policy changed.

NEXT EXACT ACTION: perform the bounded local error-handling audit of persistence,
navigation, file operations, engine connection, watcher setup, and snapshot
publication. Pick one reproducible silent-failure or data-loss risk only if its
current behavior is concretely unsafe; otherwise record the paths as expected
benign/recoverable and proceed to final build/verifier/OpenSpec gates.

Slice 14 is implemented in the working tree: `UiServer::Start` now rolls back
the snapshot mapping and connection gate when security-descriptor or listener
startup fails, and clears the owned descriptor after a pipe-name collision.
The concrete failure was reproduced by squatting the exact UI pipe name: before
the fix, `OpenFileMappingW` could still open the server's named snapshot after
`Start` returned false; the failed object also had no safe retry boundary. A
focused lifecycle test now verifies the mapping is gone and that the same
server can start after the collision is released. The focused CTest passed and
the test passed through ten direct CLI repetitions. Product commit:
`72516f0` (`Rollback failed UI server starts`).

Bounded error-handling audit conclusion: `NavigationWorkspace` uses atomic
replacement and preserves the prior state on write failure; malformed state
falls back without blocking startup. File-operation submission, shell callback,
conflict, cancellation, and permission failures become completion/cancelled
events rather than silent success, with overwrite guarded by the transfer plan.
`EngineClient` fails pending callbacks on disconnect/write failure and retains
queued directory requests across reconnect; `DirectoryWatcher` re-enumerates
on overflow and records terminal worker failures; `SnapshotPublisher` returns
zero on capacity/unstarted publication and `UiServer` exposes publish failure
to the requesting/subscribed UI. No additional evidence-backed local defect
was found in those paths during this bounded pass.

Final gates complete after Slice 14: Debug, Analyze, and Release builds passed;
serial Debug and Release CTest passed 49/49 each; `verify.ps1 doctor` reported
the installed compiler/SDK/CMake/Ninja and diagnostics prerequisites available
with only optional tools/providers skipped; `verify.ps1 build` passed; and
pinned strict OpenSpec validation passed 45/45. `ffdoc_drift_tests` passed in
both CTest trees. The final CLI smoke launched the rebuilt Debug Engine and UI,
confirmed the real `FastFilesMainWindow` and read-only selection diagnostic,
observed a responsive UI, then closed the UI and stopped the exact campaign
Engine. A first probe harness class-lookup race was cleaned and is not treated
as product evidence; no new navigation claim is made from that injection.
Only the pre-existing service PID `21148` remains. Final source and docs are
committed; worktree is clean.

Remaining local engineering frontiers: NONE. External gates remain: authorized
production signing and non-placeholder peer pins; signed/elevated privileged
MFT/USN validation; clean-host/provider validation; and direct visual/interactive
acceptance including the Treemap `% of Parent` assertion. Do not repeat the
inconclusive large benchmark timeout or reopen these external blockers.

NEXT EXACT ACTION: none — deliver the final campaign report; do not merge to
main or push.

## Overnight lifecycle frontier — no product defect (2026-08-12)

- Source review covered `EngineClient::Start/Stop`, the bounded
  `ManagementLoop`, `ConnectAndSubscribe`, `WindowShell::Run`, and the Engine
  parking contract in `src/engine/src/Main.cpp`. UI shutdown stops the UI-side
  client and closes the UI window; the Engine is intentionally a per-logon
  background helper and is not owned for termination by the UI.
- Diagnostic runtime probe: UI PID `32800`, main HWND `1181734`, initial exact
  diagnostic Engine PID `34700`, replacement PID `13500`. After terminating
  only PID `34700` after executable-path validation, the UI observed the
  replacement and remained non-hung; `WM_CLOSE` exited the UI normally.
- The replacement Engine remained after UI close by design. The exact
  campaign-created diagnostic Engine was then terminated and final exact-path
  process checks were empty. The first probe's process matcher missed the
  `\\?\\` Win32 executable-path prefix and left PID `33408`; that harness
  artifact was identified and cleaned immediately, with no source impact.
- Normal production Debug launch remains fail-closed because the local Engine
  is unsigned; the supported diagnostic opt-in launched and connected as
  designed. No stale-state, duplicate-launch, reconnect, hang, or product
  cleanup defect was established. No source/product commit is needed for this
  frontier.

## Overnight slice 6 — case-insensitive snapshot path identity implementation

- Severity: MEDIUM UI correctness/live-refresh risk in degraded browsing, with
  a direct connection to the committed case-insensitive watcher-key fix.
- Source proof: `UiServer::directories_` and `SnapshotFormat` use the default
  case-sensitive `std::map`; `UiServer::OnDirectoryChanged` receives the first
  watched spelling, while `ColumnView::RefreshColumnFromSnapshot`,
  `SnapshotAggregate`, and `ColumnView::OnDirectoryError` use exact path
  lookup/equality. `DirectoryWatcher` now deliberately coalesces case variants
  into one watch, so the old duplicate-watch behavior can no longer mask this
  mismatch.
- Reproducer shape was reduced to deterministic ownership tests: two path keys
  differing only by ASCII case now coalesce in `CaseInsensitiveSnapshotMap`, and
  a subtree whose child key uses different casing remains a complete aggregate.
  The source path proves the live-watch variant: `DirectoryWatcher` coalesces
  the watch while `UiServer` previously stored the callback's first spelling.
- Fix: add `src/protocol/include/ffprotocol/WindowsPath.h`, use the shared
  comparator for `UiServer::directories_` and `DirectoryWatcher`, add the
  publisher serialization overload, and add `ffui::FindSnapshotDirectory` for
  UI exact-then-case-insensitive lookup. `ColumnView` error/loading matching
  and `SnapshotAggregate` cycle guards use the same identity rule.
- Regression coverage: `ffengine_snapshot_publisher_tests` verifies map
  coalescing and the custom publisher overload; `ffsnapshot_aggregate_tests`
  verifies a case-variant child; `ffengine_directory_watcher_tests`,
  `ffprotocol_tests`, and `ffprotocol_fuzz_tests` remain green. Focused result:
  5/5. No selection volume/FRN identity semantics changed.

## Overnight slice 7 — UI-server callback lifetime race fix

- Severity: HIGH lifecycle/reliability risk. A five-run direct CLI stress batch
  of `ffengine_ui_server_lifecycle_tests` reproduced one access violation
  (`0xC0000005`); the other four runs passed. The same test passed once under
  CTest and then failed once in a six-test focused CTest batch, so this is a
  timing-sensitive race rather than a deterministic assertion failure.
- Root cause path: `PipeListener::Start` creates detached threads for
  `onConnect_`. `UiServer::HandleConnection` can still remove its client from
  `clientsMutex_`, use `snapshot_`, or close its pipe after `UiServer::Stop`
  has returned. `UiServer::Stop` currently stops the listener and then destroys
  snapshot/client state without a callback-lifetime barrier. The fault address
  symbolized into `std::_Iterator_base12::_Adopt_unlocked`, consistent with a
  detached callback racing container teardown.
- Fix implemented: add a shared UI-server connection gate captured by
  the listener callback. Callbacks register before touching `this`, callbacks
  arriving after stop close their pipe without dereferencing the server, and
  `UiServer::Stop` marks the gate stopping, cancels subscribed clients, stops
  accepting, and waits for registered callbacks before snapshot/client teardown.
  Do not change the privileged service listener or pipe protocol in this slice.
- Regression validation: the existing lifecycle test passed 10/10 direct CLI
  stress runs after the fix. The focused watcher/lifecycle/publisher/aggregate
  CTest batch passed 4/4. The Debug lifecycle target and `FastFilesEngine`
  rebuilt successfully. No privileged service listener or pipe protocol changed.
- Slice status: COMPLETE; implementation and focused validation are committed as
  `7b9f8fa` (`Prevent UI server callback teardown race`).

## Overnight slice 8 — Unicode workspace persistence investigation

- Severity: MEDIUM persistence/data-integrity risk. A new deterministic extension
  of `ffnavigation_tests` saves a bookmark, last location, and recently closed
  tab containing `日本語`, `é`, `Ω`, and an emoji, then reloads the state. The
  pre-fix test fails both the save and round-trip assertions.
- Root cause: `NavigationWorkspace::SaveState` and `LoadState` use locale-
  dependent `std::wofstream`/`std::wifstream` for a user path-bearing JSON file.
  The existing `/utf-8` compiler setting only governs source decoding and does
  not make these runtime streams UTF-8.
- Fix: serialize the existing JSON document to explicit UTF-8 bytes and decode
  UTF-8 bytes before the existing parser, retaining empty/default-state fallback
  for unreadable, invalid-UTF-8, or malformed content. The JSON schema and path
  identity semantics are unchanged.
- Regression validation: Unicode path/name/emoji round-trip and invalid UTF-8
  fallback cases pass in `ffnavigation_tests`. Debug focused build/test and
  Analyze `ffnavigation_tests`/`FastFiles` build pass with no warnings. Slice
  status: COMPLETE; product commit `66bffd0` (`Preserve Unicode workspace state`).

## Overnight slice 9 — in-batch transfer Replace collision investigation

- Severity: HIGH data-integrity risk. `BuildTransferPlan` reserves destination
  keys across one batch, but its `Replace` branch still emits an empty
  `destinationName` for a collision caused only by an earlier planned item.
  Two selected files with the same basename can therefore be submitted to the
  same destination path, allowing the later transfer to overwrite the earlier
  one.
- Planned reproducer: pass two same-named sources to an empty destination and
  return `ConflictChoice::Replace` with apply-to-all. The safe invariant is that
  every non-skipped plan item has a distinct destination; an in-batch collision
  must use a generated keep-both name even when Replace was chosen for an
  already-existing destination.
- Planned fix: distinguish planned occupancy from pre-existing occupancy in the
  policy seam. Preserve explicit Replace only for a destination that existed
  before this batch; resolve an in-batch collision with a non-colliding generated
  name. Do not weaken user-confirmed Replace for an external existing item.
- Fix: the planner now separates `plannedCollision` from `externalCollision`.
  Replace remains available for an existing destination, while a planned
  collision falls through to the existing keep-both name generator.
- Regression validation: the new policy case fails before the fix and passes
  after it; the external-Replace preservation case passes; the real
  `fffileoperations_e2e_tests` fixture confirms both same-named sources retain
  distinct contents under Replace/apply-to-all. Debug focused policy/e2e tests
  and Analyze policy build pass. Slice status: COMPLETE; product commit
  `273dd2d` (`Prevent in-batch transfer overwrites`).

## Overnight slice 10 — pending-selection path identity investigation

- Severity: MEDIUM UI correctness risk directly downstream of the committed
  Windows snapshot path-identity seam. `ColumnView::RefreshColumnFromSnapshot`
  applies a pending selected result only when `column.path == pendingPath`,
  while search/file-operation paths and watcher/snapshot keys can differ only
  by case on Windows.
- Reproducer shape: open a result whose requested parent is `c:\fixture` while
  the refreshed column is keyed as `C:\Fixture`; the row data refreshes but the
  pending result selection is never applied because the exact comparison fails.
- Fix: `MatchesPendingSnapshotPath` now applies `ffprotocol::WindowsPathEqual`
  while preserving the existing empty-parent unconstrained behavior; the
  ColumnView refresh path consumes this helper.
- Regression validation: focused `ffsnapshot_aggregate_tests` passed with equal,
  empty, and unequal parent-path cases; the real `FastFiles` target and its
  Analyze target rebuilt successfully, and `ffui_text_encoding_tests` remained
  green. Slice status: COMPLETE; product commit `7edef8c` (`Preserve pending
  selection across path casing`).

## Overnight slice 11 — Unicode transfer identity investigation

- Severity: HIGH data-integrity risk if confirmed. `FileOperationPolicy` derives
  planned destination keys with locale-dependent `std::towupper`, whereas the
  Windows path identity authority is `CompareStringOrdinal(..., TRUE)`. A
  non-ASCII case pair such as `é.txt` and `É.txt` may therefore bypass the
  reservation set and target one Windows destination twice.
- Planned reproducer: two sources with those same-Windows-name case variants,
  an empty destination, and no external conflicts must still yield two distinct
  destination names under the transfer policy.
- Fix: normalized destination keys now live in a `std::set` using
  `ffprotocol::CaseInsensitivePathLess`, so reservation identity follows
  `CompareStringOrdinal(..., TRUE)` rather than locale-dependent uppercasing.
  The direct file-operation test targets now include the protocol public header
  directory required by this shared authority.
- Regression validation: the new `é.txt`/`É.txt` policy case fails before the
  fix and passes after it; the existing in-batch Replace and external Replace
  cases remain green; real `fffileoperations_e2e_tests` passes. Debug focused
  policy/e2e and Analyze policy/e2e builds pass. Slice status: COMPLETE; product
  commit `2c30a7c` (`Use Windows identity for transfer reservations`).

## Overnight slice 1 — reserve destinations within one transfer batch

- Severity: HIGH correctness risk for copy/move batches with duplicate source
  filenames from different folders; the pre-fix planner could assign the same
  destination to multiple sources under `FOF_NOCONFIRMATION`.
- Reproducer: the new deterministic policy case passes two distinct source
  paths named `duplicate.txt` into an empty destination and requires the
  second plan item to become `duplicate (2).txt`. Before the fix it failed and
  never invoked the conflict prompt.
- Root cause: `BuildTransferPlan` consulted only the external `exists` callback;
  it did not reserve destinations already assigned earlier in the same plan.
- Fix: keep normalized, case-insensitive Windows destination keys for all
  planned non-skipped outputs; use that reservation set in conflict and
  keep-both candidate checks.
- Focused validation: Debug `fffileoperations_tests` passed after the expected
  pre-fix failure; Debug `fffileoperations_e2e_tests` passed, including copy,
  move, rename, recycle/restore, conflict, cancellation, vanished/locked
  sources, permission failure, and link operations.
- Analyze Debug targets passed with no warning/error output; the focused Debug
  policy and end-to-end tests passed 2/2. Final full Debug/Analyze/Release
  validation remains a campaign-end gate, not a reason to reopen this slice.

## Overnight slice 2 — Details folder aggregate freshness investigation checkpoint

- Priority: HIGH UI correctness/data freshness risk; this is a direct match for
  the campaign requirement that Details must not show metadata from a previous
  selection or stale snapshot.
- Source evidence: `WindowShell::RenderDetails` caches the last folder request
  only by `(volumeRowId, fileIdLow, fileIdHigh)`. The `WM_APP_SNAPSHOT_UPDATED`
  handler refreshes ColumnView/Search/Storage but does not invalidate that key,
  so a selected folder whose children change can retain the old item count and
  total size indefinitely. Selection clearing/reselecting the same folder also
  does not reset the key because `RefreshSelectionPresentation` only manages
  preview state. `WM_APP_FOLDER_AGGREGATE` correctly gates late replies by
  request ID, so invalidation can safely reject an older response.
- Related source points: `src/ui/src/WindowShell.cpp` (`RenderDetails`,
  `RefreshSelectionPresentation`, `WM_APP_SNAPSHOT_UPDATED`, and
  `WM_APP_FOLDER_AGGREGATE`) and `src/ui/src/EngineClient.cpp`
  (`RequestFolderAggregate`). The engine reads the current projection when a
  request arrives; the missing piece is the UI refresh trigger.
- Baseline status at the investigation checkpoint: no source change had been
  made yet. The focused `ffnavigation_tests` and
  `ffengine_directory_watcher_tests` baseline passed 2/2; the implementation
  and final focused validation are recorded in the completion section below.

## Overnight slice 2 — Details folder aggregate freshness implementation

- Root cause confirmed: `ColumnView::OnSnapshotUpdated` refreshes rows from a
  newer published generation without changing a selected folder's volume/FRN
  identity, while `WindowShell::RenderDetails` previously treated that
  identity as an indefinitely valid aggregate cache key. Clearing selection or
  selecting a file also left the old key alive.
- Fix: `FolderAggregateRequestState` is the small UI-thread-owned request seam.
  Snapshot publication invalidates it; a non-directory/no selection invalidates
  it; a different folder identity starts a new request; and old posted replies
  are rejected by request ID after invalidation.
- Regression coverage: `ffdetails_aggregate_state_tests` covers initial reuse,
  forced refresh after invalidation, late-result rejection, and selection-key
  changes. The focused aggregate/snapshot/navigation/watcher set passed 5/5.
- Validation: Debug FastFiles and the new test built; Analyze FastFiles and the
  new test built cleanly. Product commit: `9b3828d` (`Refresh Details folder
  aggregates after state changes`). Full campaign gates remain deferred until
  the next bounded slices are complete.

## Overnight slice 3 — watcher case-insensitive key investigation

- Severity: MEDIUM reliability/resource risk in degraded browsing. Windows
  treats directory paths case-insensitively, but `DirectoryWatcher` used raw
  `std::wstring` keys for both its watch map and LRU deque. Two casing variants
  could therefore open two overlapped directory handles, invoke two callbacks,
  republish duplicate work, and consume two of the 32 watch slots.
- Reproducer: `TestCaseVariantPathIsIdempotent` registered the same disposable
  directory once with its normal path and once with an upper-case variant,
  created one file, and observed both callbacks before the fix. The focused
  `ffengine_directory_watcher_tests` failed only the new idempotence assertion;
  all pre-existing watcher lifecycle, LRU, overflow, and give-up assertions
  passed.
- Next implementation: use ordinal case-insensitive path equality for map
  lookup and every LRU/de-registration operation, while retaining the first
  registration's callback/path for the existing idempotent contract.

## Overnight slice 3 — watcher case-insensitive key implementation

- Root cause confirmed by the new failing test: the raw-key `std::map` admitted
  both normal and upper-case spellings of one Windows directory, so both
  overlapped workers delivered the same file change.
- Fix: `CaseInsensitivePathLess` uses `CompareStringOrdinal(..., TRUE)` for
  map identity; LRU refresh, replacement, and `Unwatch` use the same ordinal
  case-insensitive equality. The first registration's callback and path remain
  authoritative, and `UnwatchAll` still moves the correctly-typed map while
  holding the mutex before joining workers.
- Validation: the new real-filesystem reproducer passed, including canonical
  callback-only delivery; the existing watcher publish/restart, LRU eviction,
  injected overflow, give-up, navigation, and Details aggregate tests passed
  3/3; the Analyze watcher target built cleanly. Product commit: `e5352d1`.

## Overnight slice 4 — workspace-state persistence atomicity investigation

- Priority: HIGH persistence/recovery correctness. `NavigationWorkspace::SaveState`
  currently opens `workspace-state.json` with `std::ios::trunc` and streams the
  replacement directly into the live file. A process termination, power loss,
  or write error after truncation can therefore leave the only recoverable
  workspace state incomplete. This is a source-proven failure window; direct
  crash injection is intentionally not claimed.
- Existing repository precedent: `ShortcutMap::Save` already writes a sibling
  `.tmp` file and uses `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH`, preserving the old target until the replacement is
  ready. The workspace persistence path should use the same bounded policy.
- A prior test already proves normal save/load and corrupt-file fallback. The
  next step is to add deterministic success/temporary-artifact checks, implement
  the atomic replacement, and verify the navigation target in Debug and Analyze.

## Overnight slice 4 — workspace-state persistence atomicity implementation

- Root cause confirmed by source audit: workspace persistence truncated the live
  file before serialization completed, unlike the already-hardened shortcut
  persistence path. A crash/write failure could destroy the last usable state.
- Fix: serialize to `workspace-state.json.tmp`, close/check the stream, then
  replace the target with `MoveFileExW(MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH)`; failed serialization/replacement removes the temp
  file and leaves the old target untouched.
- Regression coverage: navigation persistence now asserts the sibling temporary
  replacement is absent after a successful flush; existing save/load and corrupt
  fallback coverage remains in the same test.
- Validation: Debug and Analyze `ffnavigation_tests` passed; product commit
  `4cb44e3` (`Atomically save workspace state`). Direct power-loss behavior was
  not simulated or claimed.

## Overnight slice 5 — workspace restore bound implementation

- Root cause confirmed: `LoadState` bypassed the same 16-entry limit enforced by
  `CloseActiveTab` and could restore all entries from a malformed local array.
- Fix: the parser stops adding entries at `kMaxRecentlyClosedTabs`, retaining
  the newest entries in the persisted front-to-back order without changing the
  supported runtime queue contract.
- Regression: the disposable navigation profile writes 20 persisted entries
  and requires exactly the first 16 to load. Before the fix the focused
  `ffnavigation_tests` failed this assertion; after the fix it passed.
- Validation: Debug and Analyze `ffnavigation_tests` passed; product commit
  `db7adf3` (`Bound restored recently closed tabs`). A separate maximum for
  persisted bookmarks remains a `SPEC_AMBIGUITY`/deferred item because no
  product limit exists in the current contract.

## Overnight slice 5 — workspace restore bound investigation

- Severity: MEDIUM persistence/recovery resource risk. Runtime close-tab logic
  enforces `kMaxRecentlyClosedTabs == 16`, but `LoadState` appended every
  `recentlyClosedTabs` entry from disk without that bound. A malformed or stale
  local profile could therefore restore an arbitrarily large queue and make
  startup/reopen behavior diverge from the supported model.
- Reproducer plan: persist more than 16 closed-tab entries in the disposable
  navigation profile and assert the loaded queue exceeds the runtime bound
  before the fix. The implementation should retain only the newest 16 entries
  in file order, matching `push_front` + `pop_back` semantics.

## Session continuation reconciliation (2026-08-12)

- The worktree was clean on `recovery/fast-files-20260809` at the preceding
  product commit `66c45f2` (`Fixes`); the documentation and hygiene checkpoints
  are recorded in the commits immediately following it.
- The earlier current-session notes that described changes as uncommitted at
  `4196a5d` are stale; those bounded-probe, path-identity, diagnostic,
  validator, USN V2/V3, watcher-startup, snapshot-capacity, and ingestion fixes
  are included in `66c45f2`.
- This continuation will not use Computer Use or alter signing pins. The next
  exact action was to rerun the x64 Debug/Analyze/full CTest local gates, then
  audit merge readiness and retain only the genuine signed/elevated/clean-host/
  aggregate-backed external blockers.
- Revalidation passed: Debug configure/build clean; Debug CTest 47/47; Analyze
  configure/build clean; Release CTest 47/47; `verify.ps1 doctor` passed all
  available toolchain/SDK/Application Verifier checks; `verify.ps1 build`
  passed `windows-build-validation`; and pinned OpenSpec strict validation
  passed 45/45.
- Release fail-closed validation passed in a fresh unprovisioned cache: CMake
  rejected absent thumbprints before generation. The branch range has no merge
  conflict markers; the only hygiene defect found was the extra EOF blank line
  in `ENGINEERING_AUDIT_REPORT.md`, now removed.
- Genuine blockers remain unchanged: authorized production signing material and
  signed active/degraded service validation, expired-signer release evidence,
  clean-host privilege matrix, and the real aggregate-backed `% of Parent`
  visual sum. No pins were changed and no Computer Use was used.
- Merge-range hygiene also found eight root-level compiler `.obj` files and the
  one-off `write_report2.py` report writer plus one tracked `__pycache__/*.pyc`
  file committed by the migration snapshot; none is referenced by source/build
  guidance. They are removed, and `.obj`, Python bytecode, and `__pycache__`
  are now ignored.
- The full OpenSpec pending-task audit confirms the remaining autonomous/
  zero-touch rows are environment-gated only: disposable-provider tasks
  `autonomous-runtime-verification` 2.3–2.4 and
  `zero-touch-autonomous-engineering` 6.3–6.4/6.6 require a supported Windows
  guest, installation media, secure guest channel, and snapshot-restorable
  evidence. Current `verify.ps1 doctor` reports Hyper-V/Sandbox unavailable;
  no Windows media, guest OS, or WinRM channel is available. Those rows remain
  `[ ]` with their machine-backed `REQUIRED-BUT-UNAVAILABLE` notes.
- Final handoff audit before this documentation-only checkpoint: HEAD was
  `259224e`; the worktree was clean; the branch was
  112 commits ahead and 2 behind `main`; `git diff --check main...HEAD` passes;
  `git merge-tree` reports no conflict markers; no FastFiles or
  FastFilesEngine process remains; and `ffdoc_drift_tests` passes after the
  artifact cleanup. The branch has not been pushed; it is 5 commits ahead of
  `origin/recovery/fast-files-20260809`.

## Historical signed-test validation record (2026-08-12)

- The bounded-probe, path-identity, diagnostic, validator, and focused-test
  changes described below were uncommitted at `4196a5d` during the earlier
  signed-test session; they are now included in clean commit `66c45f2`.
- During that earlier signed-test session, PowerShell was elevated and
  interactive. The registered `FastFilesIndexSvc` was running as `LocalSystem`
  with delayed automatic start. A reversible signed-test installation was
  active under
  `C:\Program Files\FastFiles`; it uses a disposable test certificate only for
  runtime validation and must be restored before handoff.
- Local doctor passes the VS toolset, Windows SDK, CMake, Ninja, and Application
  Verifier checks. Hyper-V is present but no guest OS/install media is available.
- Tier-1 privilege/IPC probes passed, and the engine-service validator now
  correctly models the LocalSystem constrained broker. The path-identity fix
  normalizes only Win32 extended-length aliases, allowing authenticated
  engine/service image comparison without weakening directory or signer checks.
- The signed-test runtime reached `active`; the service opened `\\.\C:` as
  `NT AUTHORITY\SYSTEM` with SeBackupPrivilege held and enabled. The first
  real scan exposed a `FlatHashMap` probe-bound exception; bounded grow-and-
  retry recovery is implemented and the focused hash/projection tests pass.
- After redeploying the rebuilt signed-test Engine, the process remains
  responsive and the durable entry count is increasing (424,477 observed at
  04:53; roughly 866 MB working set), with `scan_complete=0` while the
  scan is still running. The USN cursor advanced on every sample; no second
  abort, projection-pressure, or snapshot-publication failure has occurred.
- A full-suite run exposed an intermittent `DirectoryWatcher` startup race:
  `Watch` could return before its worker armed the first
  `ReadDirectoryChangesW`, losing an immediate post-browse change. A bounded
  startup handshake now releases the caller after the first overlapped read is
  armed (or the worker has failed). The focused lifecycle test passed 10/10
  repeated runs, and the full Debug suite passed 46/46 afterward.
- The rebuilt Debug tree passed the full 46-test CTest suite; the `/analyze`
  tree completed all 83 targets cleanly; and `pwsh ./verify/verify.ps1 build`
  passed after the generated Release cache was explicitly provisioned with
  ephemeral test pins. The first harness attempt correctly failed closed on
  stale empty Release cache pins; no source pin was changed.
- The 64 MiB snapshot slot was too small for the live projection: publication
  began failing at 462,744 entries with `ERROR_WRITE_FAULT` while the scan
  remained responsive. The shared protocol capacity is now an explicit,
  bounded 256 MiB per slot. The first replacement missed the Engine because
  Windows exposed its process path with a `\\?\` prefix; normalized exact-path
  stopping/copying corrected that, and the installed Engine hash now matches
  the rebuilt artifact.
- After the corrected deployment, the entry count resumed from 462,744 to
  464,225 in 50 seconds, the USN cursor advanced, Engine working set stayed
  around 637–646 MiB, and no new snapshot-publication failure appeared after
  the deployment (the last observed old failure epoch was 1786483438).
- A later runtime sample reached 685,365 entries with `scan_complete=0` and a
  stable cursor while the Engine stayed CPU-bound at roughly one full core and
  its working set oscillated around 2.5–2.8 GiB. The targeted diagnostics showed
  an earlier transient `service-not-running` reconnect cycle, but the service
  is now Running and the Engine is Active with no recent connect or snapshot
  failure. Source tracing confirms that the 500 ms throttle can still invoke a
  complete `ExportDirectorySnapshot` after every long export; that export holds
  the projection mutex, then `UiServer` serializes/copies the entire volume
  again. This is the current scan-progress plateau hypothesis, not a host-memory
  exhaustion finding.
- After installing the scan-publication fix and relaunching against the
  persisted 710,867-row database, the Engine remained in the synchronous
  startup `RebuildAll`/snapshot callback for several minutes: the cursor stayed
  at `8646672712`, working set oscillated around 3.1–5.2 GiB, and CPU advanced
  at roughly one core while the service remained Running and the UI/Engine
  connection was Active. `Main.cpp` currently exports every available rebuilt
  volume before `PrivilegedConnection.Start()`, including an incomplete scan;
  the next correction is to rebuild that projection but defer its full snapshot
  publication so scan resumption is not gated by a multi-gigabyte temporary
  export.
- With startup publication deferred, the new Engine reached `active` and the
  cursor advanced immediately, proving the startup gate works. It then stalled
  at 721,389 entries/cursor `8646877936` while CPU continued at one core and
  working set oscillated between 3.6–4.7 GiB. The projection reported roughly
  420 MiB of owned memory. Source inspection found `Projection::Upsert` invokes
  the full LRU `EnforceMemoryLimit`/`CompactStorage` path for every record;
  once the 500 MiB bound is approached, real-volume ingestion enters an O(N)
  compaction storm. The bounded policy itself remains required, so enforcement
  will move to the end of each already-committed MFT/USN batch.
- The first batch-boundary redeploy still repeated compaction roughly once per
  batch: the exact-ceiling policy had no headroom, and `CompactStorage` rebuilt
  the optional filename-trigram accelerator while the old structures were
  still resident. Samples reached 6.4 GiB working set while the cursor held.
  The next bounded correction is an 80% low-water mark plus fail-safe
  invalidation of that optional accelerator during eviction compaction; the
  canonical search path remains available when the accelerator is invalid.
- Directory-first eviction plus one compaction per batch bounded the number of
  compactions but still produced a 10 GiB working-set peak. The old trigram
  postings remain allocated until the end of `CompactStorage`, so the peak
  path still holds old postings, old projection tables, and new tables at once.
  The next correction is to invalidate/clear that optional accelerator before
  allocating the replacement projection tables.
- The final active-scan policy clears the optional filename postings and defers
  eviction compaction for the duration of initial/reconciliation scans. The
  signed-test runtime then reached `scan_complete=1`; over six samples the USN
  cursor advanced continuously, entry count remained live, and Engine working
  set stayed roughly 0.8–1.2 GiB. This is the first sustained real-volume
  evidence that ingestion no longer enters the compaction/export hot loop. The
  optional filename accelerator is intentionally invalidated for that runtime
  projection; canonical search remains the correctness path.
- The exact USN probe file was present in the included Temp subtree and its
  `fsutil usn readjournal C:` records were confirmed as USN major version 3
  (`File create` and `File create | Close`), while the Engine cursor advanced
  and no durable row appeared. `UsnJournalReader.cpp` currently casts every
  record to `USN_RECORD_V2`, so it reads the V3 128-bit-ID layout at the wrong
  offsets and drops the record at filename validation. The next implementation
  slice is a bounded V2/V3 decoder; NTFS V3 IDs have the existing 64-bit FRN in
  the low half, matching the current wire/storage model.
- The bounded V2/V3 decoder is implemented in `UsnRecordParser.{h,cpp}` and
  `UsnJournalReader.cpp` now selects by the untrusted common header, validates
  the full record/name range, and rejects non-zero V3 ID high halves instead of
  truncating. `ffusnrecord_tests` covers V2, V3, unsupported versions, malformed
  ranges, odd UTF-16 lengths, and high-half rejection. Debug build/focused CTest
  pass. After the signed service redeploy, the fresh V3 probe
  `FastFiles-usn-probe-20260812-1004-v3.tmp` appeared in SQLite on attempt 3
  and disappeared after the exact file delete on attempt 8; `scan_complete=1`
  and `resume_usn` advanced throughout. The disposable file is removed.
- Read-only `PRAGMA integrity_check` returned `ok` before restart. The exact
  installed UI/Engine pair was stopped and relaunched (new PIDs), then sampled
  six times over 25 seconds: the Engine stayed alive, the durable count stayed
  at 768,952, `scan_complete` stayed `1`, and only `resume_usn` advanced from
  8650197432 to 8650344960. This is persisted journal-resume evidence without
  a blind MFT rescan; the service remained Running.
- The focused snapshot/lifecycle tests passed, the rebuilt Debug tree and full
  CTest suite passed 46/46, the `/analyze` tree rebuilt all 62 affected targets
  cleanly, and `pwsh ./verify/verify.ps1 build` passed. Those gates predate the
  final active-scan policy; the focused regressions for the policy pass and the
  signed-test runtime now reaches scan completion without a new snapshot
  failure.
- After normalizing the ten touched mixed-line-ending files to CRLF, the full
  Debug build completed and the documented TEMP/TMP-profile CTest passed 47/47,
  including protocol fuzzing, the new USN V2/V3 test, all UI/engine/setup tests,
  and `ffdoc_drift_tests`.
- The Analyze configure/build was first interrupted after Ninja had continued
  in the background; a later incremental invocation with the required
  `FASTFILES_NINJA_EXE` path returned `ninja: no work to do` with exit 0. The
  Analyze tree is therefore complete and clean for this source state.
- The verification harness `pwsh ./verify/verify.ps1 build` passed
  (`windows-build-validation`). Cleanup then restored all four original
  installed executables byte-for-byte, left the user DB/settings intact, left
  `FastFilesIndexSvc` Running/Automatic, removed both disposable USN probe
  files, and removed the disposable test certificate thumbprint from
  CurrentUser My/Root and LocalMachine Root. No UI/Engine test process remains.
- NEXT RECOMMENDED SLICE: obtain the production signing material and an
  authorized clean-host/VM matrix, then execute the still-open external
  OpenSpec gates 4.2/4.5–4.8 and 5.1. Do not start that slice automatically;
  production signing, clean-host/VM validation, and other external evidence
  remain unresolved.

## Authenticated engine-launch hardening slice (2026-08-12)

- Root cause: engine discovery validated only regular-file existence, so a
  planted or reparse-routed executable could reach `CreateProcessW`; release
  builds also had no build-time enforcement that both Authenticode pins were
  provisioned.
- Bounded implementation: CMake now generates pin constants from signing-pipeline
  cache/environment inputs and refuses absent/all-zero Release configuration;
  `PinnedSignatures.h` keeps the all-zero development fallback; UI, Engine, and
  Index Service perform non-debug startup self-checks. Discovery now rejects
  reparse/canonical-path mismatches and requires a pinned Authenticode signer
  before launch. Structured launch/rejection diagnostics retain the path and
  observed signer thumbprint.
- Focused validation: Debug configure/build passed; Release configure failed as
  designed without signing inputs; the Engine, Index Service, UI, Authenticode,
  discovery, and diagnostics targets built cleanly with `/W4 /WX`; focused CTest
  passed 3/3. Direct discovery execution passed with the default 8.3 TEMP path,
  unsigned rejection, and symlink rejection. Full Debug CTest passed 45/45,
  Analyze built cleanly, and strict OpenSpec validation passed 45/45. Computer
  Use confirmed strict unsigned Debug reaches the unavailable state and the
  explicit diagnostic build preserves degraded browsing; the exact diagnostic
  Engine was cleaned up.
- No production certificate, thumbprint, signing bypass, or privileged-path
  claim was introduced. Evidence is recorded in
  `openspec/changes/address-engineering-audit-findings/evidence/authenticated-engine-launch-20260812.md`.
- Slice status: COMPLETE; commit `ee01486` contains the implementation, tests,
  evidence, and task reconciliation. Release configuration is intentionally
  blocked until signing inputs exist. External signing, elevated service, real
  MFT/USN, privilege-diagnostics, clean-host, and aggregate-backed visual
  evidence blockers remain unresolved.
- NEXT EXACT ACTION: audit the remaining OpenSpec blocker rows and record only
  machine-backed `REQUIRED-BUT-UNAVAILABLE` evidence; do not alter signing
  material or begin an unrelated product slice.

## Authorized system-level visual matrix (2026-08-11)

- The user explicitly authorized temporary Windows display changes and direct
  Computer Use. The pass exercised the rebuilt Debug `FastFiles.exe` at 150%
  DPI, Windows High Contrast (`Aquatic`), animations disabled, Light/Dark
  themes, details open/closed, single/dual pane, and a 724 px narrow window.
- Retained screenshot/evidence artifacts are in the untracked directory
  `verify/runs/manual-visual-20260811/`; the tracked acceptance record is
  `openspec/changes/close-independent-validation-gaps/evidence/interactive-visual-validation-20260811.md`.
- Direct observations found no visual defect: text remained readable, no
  mojibake or column-under-details overlap was observed, High Contrast used
  readable system-color chrome, reduced-motion remained stable, and the
  narrow strategy removed the full details card without making the columns
  unusable. The 100% final screenshots show the reserved details region and
  the single/dual-pane layout.
- The real Analyze Storage → Drill Down UI exposed the `% of Parent` column,
  but degraded/partial coverage supplied `—` for file rows and `Partial` for
  folder aggregates. The required real 100%-sum assertion is therefore not
  claimed; OpenSpec task 5.1 remains open for that data-dependent row. Tasks
  5.2 and 5.3 are now checked with the retained evidence.
- Final host display state was restored and verified: Display Settings showed
  100%, FastFiles/Settings window DPI was 96, `HighContrast=False`, and
  `Animations=True`. FastFiles and the exact sibling Engine were cleaned up.
  A later local-gate audit found the separately registered automatic
  `FastFilesIndexSvc` running from `C:\Program Files\FastFiles\`; stopping it
  returned Access Denied from both PowerShell and `sc.exe`. Its registration,
  start mode, binary path, and security configuration were left unchanged;
  stopping this in-scope service requires an elevated service-control action.
- Fresh resume on 2026-08-12 confirmed the current Codex shell still reports
  `IsAdmin=False` even after the user changed the account context. A narrowly
  scoped `Start-Process -Verb RunAs` attempt for only this service timed out
  after 30 seconds without changing the service; UAC/secure-desktop approval or
  a newly launched elevated Codex session is required. No elevation bypass was
  attempted.
- Final gates after the visual pass: pinned OpenSpec strict validation passed
  45/45; x64 Debug, Analyze, and Release builds passed; serial Debug CTest
  passed 44/44; serial Release CTest passed 44/44; and
  `pwsh ./verify/verify.ps1 build` passed the `windows-build-validation`
  capability. No source or security behavior changed in this pass.
- The tracked evidence/task/workstate reconciliation is committed as
  `043b679` (`docs: record authorized visual validation`). The exact
  remaining OpenSpec blockers are 4.2's
  expired/untrusted/release-pin evidence, 4.5–4.8's signed/elevated/real
  MFT-USN/privilege/clean-host evidence, and 5.1's real aggregate-backed
  `% of Parent` sum. Do not alter signing material or pins.
- Resume audit: the worktree was clean at `c5ca5358` before the current
  FlatHashMap slice; `verify.ps1 doctor` still passes the local VC toolset,
  SDK, CMake, Ninja, and Application Verifier checks, while Hyper-V/Sandbox
  are unavailable for disposable-host validation. The previously observed
  orphaned `FastFilesIndexSvc.exe` PID 9708 has since exited; no Fast Files
  SCM service is registered. The FlatHashMap implementation, CTest target,
  OpenSpec evidence, and task/workstate checkpoint were committed as
  `3619c26`/`53530be`. The current snapshot-publisher contention slice is the
  only uncommitted frontier.

## FlatHashMap safety slice (2026-08-11)

- Root cause: `FlatHashMap::operator[]` never grew its bucket array, while
  `find_slot` used an unbounded linear-probe loop. A full or adversarially
  colliding table could therefore spin indefinitely.
- Bounded fix: grow before insertion at the 75% live-key threshold, cap each
  probe at 64 steps, and throw a clear `std::runtime_error` when insertion
  cannot find a slot within that bound. `EntryKeyHash` now applies a final
  64-bit mix so ordinary sequential FRNs do not reach the adversarial bound.
  Existing keys are rehashed during growth; no security, IPC, signing, or
  authentication behavior changed.
- Regression coverage: new `ffindexstore_flat_hash_map_tests` covers repeated
  `operator[]` growth beyond the old capacity and a constant-hash collision
  sequence that reaches the bounded abort. The 4,100-record engine projection
  fixture also guards the production hash distribution. Focused CTest passed
  2/2; serial Debug and Release CTest each passed 45/45; Debug, Analyze, and
  Release builds passed; the Windows build verifier passed; and pinned
  OpenSpec strict validation passed 45/45.
- Evidence is recorded in
  `openspec/changes/address-engineering-audit-findings/evidence/data-structure-safety-20260811.md`.
  Tasks 3.1–3.4, 8.2, and 8.5 are checked. No signing pins or authentication
  controls were changed.
- Slice status: COMPLETE; do not restart this FlatHashMap work.

## Snapshot versioning safety slice (2026-08-11)

- Root cause: the shared memory mapping carried generation and slot
  descriptors but no format version, so a mismatched engine/UI build could
  parse an incompatible layout as if it were current.
- Bounded fix: `SnapshotSharedHeader` now carries a versioned, naturally
  aligned header; `SnapshotPublisher` writes the version at startup and on
  every publication; `EngineClient` rejects incompatible versions before
  parsing, unmaps the read-only view, and remaps the retained section name on
  the next generation notification. Failed remaps retain the name for retry.
- Focused Debug validation: `ffengine_snapshot_publisher_tests` and
  `ffsnapshot_freshness_tests` passed; the real `FastFiles` UI target built.
  Full Debug/Analyze/Release builds passed; serial Debug and Release CTest
  passed 45/45; `pwsh ./verify/verify.ps1 build` passed; and pinned
  `openspec validate --all --strict` passed 45/45. The document-drift guard
  passed after the changed files were normalized to the repository's CRLF
  convention. Tasks 4.1–4.4 are checked and evidence is recorded in
  `openspec/changes/address-engineering-audit-findings/evidence/snapshot-versioning-20260811.md`.
- Slice status: COMPLETE; implementation/evidence committed as `855f0c1`.
  NEXT EXACT ACTION: begin the next local OpenSpec task, keeping the external
  signing, service, and real-index evidence blockers explicitly unresolved.

## Index-store ANALYZE safety slice (2026-08-11)

- Root cause: the ingestion pipeline committed large MFT/USN batches without
  refreshing SQLite planner statistics, so query planning could remain stale
  after index growth.
- Bounded fix: `Store::Analyze()` executes `ANALYZE`; `IndexPipeline` counts
  committed MFT records and USN upserts, triggers at 10,000, retries after a
  maintenance failure, and resets the counter at store-session boundaries.
  No security, IPC, signing, or privileged-service behavior changed.
- Focused validation: `ffengine_index_pipeline_tests` and its CTest entry pass;
  the test observes no `sqlite_stat1` at 9,999 records and observes planner
  statistics after 10,000. Evidence is recorded in
  `openspec/changes/address-engineering-audit-findings/evidence/index-store-analyze-20260811.md`.
  Task 5.1 is checked.
- Additional local repair: `FileOperations` now reports the verified
  destination/name when Shell omits or canonicalizes the NewItem callback path,
  and structurally suppresses Move undo history for any Replace plan. Focused
  `fffileoperations_e2e_tests` passes after this repair. Evidence is recorded
  in `openspec/changes/file-operations/evidence/windows-e2e-hardening-20260811.md`.
- Full Debug and Release CTest passed 45/45; full Debug/Analyze/Release builds
  passed; strict OpenSpec validation passed 45/45; and
  `pwsh ./verify/verify.ps1 build` passed `windows-build-validation`.
- Slice status: COMPLETE; implementation/evidence committed as `f8ef012`.
  NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  keeping external signing, service, and real-index evidence blockers
  unresolved.

## Index-store VACUUM safety slice (2026-08-11)

- Root cause: SQLite planner maintenance was covered, but expensive space
  reclamation had no idle, interval, or size-growth gate and no persisted
  configuration values.
- Bounded fix: `Store::VacuumIfDue` tracks the database baseline and last
  VACUUM, rejects non-idle/too-early/insufficient-growth calls, and is invoked
  only from the existing IdleManager callback. Settings defaults are 24 hours
  and 10 percent, with JSON round-trip support. No security, IPC, signing, or
  privileged-service behavior changed.
- Focused validation: `ffindexstore_store_tests`, `ffprotocol_tests`, and
  `ffengine_index_pipeline_tests` passed 3/3. Full Debug/Analyze/Release
  builds passed; serial Debug and Release CTest passed 45/45; strict OpenSpec
  passed 45/45; and the Windows build verifier passed. Tasks 5.2–5.4 are
  checked; evidence is recorded in
  `openspec/changes/address-engineering-audit-findings/evidence/index-store-vacuum-20260811.md`.
- Slice status: COMPLETE; implementation/evidence committed as `8ac1261`.
  NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  keeping external signing, service, and real-index evidence blockers
  unresolved.

## Structured diagnostics slice (2026-08-11)

- Root cause: production diagnostic lines carried human-readable state and
  Win32 error values but no stable machine-readable diagnostic identifier, so
  monitoring had to parse free-form text and could not reliably distinguish
  cases such as corrupt SQLite recovery.
- Bounded fix: `ffprotocol::DiagnosticCode` is an append-only integer enum
  with stable `FFDIAG_*` names. Every current production emission now sets an
  explicit code, including `FFDIAG_CORRUPT_DB` before the integrity-failure
  rebuild fallback. `SerializeDiagnosticEvent` and the
  `DiagnosticFormat::Json` append option emit JSON Lines with escaped fields,
  both code forms, and a human-readable message/state. Bundle redaction parses
  both text and JSON lines so the default export remains path-redacted.
- Focused validation: `ffprotocol_diagnostics_tests` built and passed 1/1,
  covering text fields, JSON fields/escaping, JSON stream append, and bundle
  compatibility. Full Debug and Release CTest passed 45/45; Debug, Analyze,
  and Release builds passed; `pwsh ./verify/verify.ps1 build` passed
  `windows-build-validation`; and strict OpenSpec validation passed 45/45.
  No IPC, signing, privilege, authentication, or trust-store behavior
  changed.
- Evidence: `openspec/changes/address-engineering-audit-findings/evidence/
  structured-diagnostics-20260811.md`; tasks 7.1–7.4 are checked in the
  change task file.
- Slice status: COMPLETE; implementation/evidence committed as `fba7ff5`.
- NEXT EXACT ACTION: continue with the next local OpenSpec task while keeping
  external signing, service, and populated-index blockers unresolved.

## Projection eviction slice (2026-08-11)

- Root cause: the projection had no bounded memory policy, so large indexed
  volumes could retain dense entries, child indexes, name storage, and search
  postings indefinitely; an eviction without coordinated reload would also
  break parent/path reconstruction.
- Bounded fix: `Projection` now defaults to a 500 MiB owned-memory limit,
  accounts its backing capacities, tracks access recency, evicts an LRU
  subtree while preserving root preference, compacts all dependent indexes,
  and marks the affected volume for durable replay. SQLite-backed replay is
  available for volume-specific and all-volume engine access paths. The engine
  emits one structured memory-pressure warning per pressure episode.
- Focused validation: `ffindexstore_projection_tests` and
  `ffengine_index_pipeline_tests` built and passed 2/2, covering LRU ordering,
  eviction markers, SQLite reload restoration, and pressure signaling.
- Evidence: `openspec/changes/address-engineering-audit-findings/evidence/
  projection-eviction-20260811.md`; tasks 6.1–6.5 are checked.
- Slice status: COMPLETE; implementation, task updates, and evidence are
  committed as `96b92be` (`Bound projection memory with durable reload`).
  Debug/Analyze/Release builds passed, serial Debug and Release CTest passed
  45/45, the Windows build verifier passed, and strict OpenSpec validation
  passed 45/45.
- NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  leaving external release blockers unresolved.

## SnapshotPublisher contention slice (2026-08-11)

- Root cause coverage: the shared-memory publisher already protects concurrent
  writers with its publication mutex, but the audit change had no bounded CTest
  proof that contended callers receive an intact generation sequence.
- Regression coverage: eight worker threads publish 64 snapshots each after a
  synchronized start. The test checks all 512 returned generations are unique
  and contiguous, verifies `CurrentGeneration()`, and parses the final active
  slot through a read-only mapping.
- Focused validation: `ffengine_snapshot_publisher_tests` passed, including
  five consecutive repeated runs.
- Full validation: Debug/Analyze/Release builds passed; serial Debug and
  Release CTest passed 45/45; the Windows build verifier passed; strict
  OpenSpec validation passed 45/45; and the doc-drift/line-ending guard passed
  after CRLF normalization.
- Evidence: `openspec/changes/address-engineering-audit-findings/evidence/
  snapshot-publisher-contention-20260811.md`; task 8.4 is checked.
- Slice status: COMPLETE; implementation, task update, and evidence are ready
  for commit. No shared-memory, signing, authentication, or security policy
  was changed.
- NEXT EXACT ACTION: review and commit this slice, then continue the external
  blocker audit without altering signing material or production pins.

## IPC reliability slice (2026-08-11)

- Root cause: the service Ctrl-pipe control loop polled `PeekNamedPipe` every
  2 ms and then entered synchronous framing reads, adding idle CPU work and
  leaving the service-side pipe mode inconsistent with the intended async
  design.
- Bounded fix: service Ctrl-pipe instances now use `FILE_FLAG_OVERLAPPED`;
  connection, handshake, header/payload reads, and service writes use event
  completion plus `GetOverlappedResult`. Idle and handshake deadlines cancel
  pending operations; the existing watchdog cancels stalled writes with
  `CancelIoEx` while preserving the synchronous cancellation fallback.
- Focused validation: `ffipc_framing_tests` passed the maximum-frame and
  malformed-header cases plus overlapped completion, timeout, service-write,
  and synchronous fallback cases. `FastFilesIndexSvc` also built.
- Evidence: `openspec/changes/address-engineering-audit-findings/evidence/
  ipc-reliability-20260811.md`; tasks 2.1–2.3 are checked.
- Full validation: Debug/Analyze/Release builds passed; serial Debug and
  Release CTest passed 45/45; the Windows build verifier passed; and strict
  OpenSpec validation passed 45/45.
- Slice status: COMPLETE; implementation, task updates, and evidence are
  committed as `b3abd9b` (`Replace service pipe polling with overlapped I/O`).
  Debug/Analyze/Release builds passed, serial Debug and Release CTest passed
  45/45, the Windows build verifier passed, and strict OpenSpec validation
  passed 45/45.
- NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  leaving external release blockers unresolved.

## Production debug-marker guard slice (2026-08-11)

- Root cause: CI had no explicit source guard against temporary debug stalls,
  so a `TEMP-DEBUG`/`DEBUG_STALL` marker could enter a production source
  path without a dedicated failure before configure/build.
- Bounded fix: `verify/check_source_markers.ps1` scans first-party C/C++
  under `src/` and fails with file/line evidence for either marker. The GitHub
  Actions workflow runs it immediately after checkout for every preset. Tests
  and audit history are outside the production-source scan boundary.
- Focused validation: the guard passed with no markers in 171 production source
  files.
- Full validation: Debug and Release CTest passed 45/45; the Windows build
  verifier passed; and strict OpenSpec validation passed 45/45. The existing
  Debug/Analyze/Release builds from the preceding IPC slice remain green; this
  slice contains no C++ changes.
- Evidence: `openspec/changes/address-engineering-audit-findings/evidence/
  production-debug-marker-20260811.md`; task 8.1 is checked.
- Slice status: COMPLETE; implementation, workflow, task update, and evidence
  are committed as `2b07d64` (`Add production debug-marker CI guard`).
  Debug/Analyze/Release builds remain green from the preceding IPC slice; Debug
  and Release CTest passed 45/45, the verifier passed, and strict OpenSpec
  validation passed 45/45.
- NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  leaving external release blockers unresolved.

## Real named-pipe handshake slice (2026-08-11)

- Root cause: the verification suite had framing and service-side async-read
  coverage, but no CTest acceptance of the Handshake/HandshakeAck exchange over
  a real Windows named pipe.
- Bounded fix: the existing real named-pipe fixture now sends a Handshake
  request through the synchronous client side, receives/parses it through the
  overlapped service-side read, then sends HandshakeAck through the overlapped
  service write helper and validates the client-side reply. No production
  authentication or signing bypass was introduced.
- Focused validation: ffipc_framing_tests passed, including exact handshake
  message types, payload sizes, and compatible protocol version.
- Full validation: Debug/Analyze/Release builds passed; serial Debug and
  Release CTest passed 45/45; the Windows build verifier passed; strict
  OpenSpec validation passed 45/45; and doc-drift/line-ending validation passed
  after CRLF normalization.
- Evidence: openspec/changes/address-engineering-audit-findings/evidence/
  ipc-handshake-20260811.md; task 8.3 is checked.
- Slice status: COMPLETE; implementation, task update, and evidence are
  committed as `4a57ba2` (`Test handshake over real named pipe`). Debug,
  Analyze, and Release builds passed; serial Debug and Release CTest passed
  45/45; the verifier passed; strict OpenSpec validation passed 45/45; and the
  doc-drift/line-ending guard passed after CRLF normalization.
- NEXT EXACT ACTION: continue with the next remaining local OpenSpec task while
  leaving external release blockers unresolved.

## Stale persisted startup location (2026-08-11)

- Computer Use reproduced a real degraded-mode startup defect: the UI restored
  the deleted visual-acceptance fixture path from `workspace-state.json`,
  leaving the shell rooted in a stale location instead of falling back to
  `This PC`/the configured startup location.
- Root cause: `WindowShell::Initialize` copied `lastLocation` directly into
  `initialNavigationPath` without parsing it or checking that it still named a
  directory. This bypassed the existing address-bar invalid-path contract.
- Bounded fix: `ResolveInitialNavigationPath` now validates persisted paths,
  restores only existing directories, preserves access-denied destinations for
  the destination view's permission state, and falls back to the configured
  startup path for deleted, malformed, or file-valued state. `WindowShell`
  uses this resolver; no IPC, engine, or security behavior changed.
- Regression coverage: `ffnavigation_tests` verifies existing-directory
  restore, deleted/malformed/file fallback, and synthetic `This PC` fallback.
  Focused test passed; Debug build passed; serial Debug CTest passed 44/44
  after one isolated transient `fffileoperations_tests` conflict-dialog
  failure (focused rerun and complete rerun passed).
- Analyze and Release builds passed; serial Release CTest passed 44/44 after
  the same isolated conflict-dialog retry pattern. Computer Use relaunched the
  rebuilt Debug UI and observed `This PC` as the restored breadcrumb with the
  stale fixture name absent; the UI was then closed and the exact sibling
  Engine process was cleaned up.
- The startup restore fix is committed as `12bdf1b`. The
  `verify/core/Toolchain.psm1` TEMP/TMP canonicalization fix and this
  checkpoint are committed in the follow-up validation commit; its full
  `verify.ps1 build` rerun passed. The CMake diagnostic guard fix is committed
  as `565b90e`.
- Final local checkpoint before this evidence commit was `5d071bb`; the
  current evidence checkpoint is `8d2c8e2`. OpenSpec
  tasks 4.2, 4.5–4.8, and 5.1 remain open because signed release material,
  clean-host privilege evidence, and a populated aggregate-backed visual
  drill-down are still unavailable; 5.2 and 5.3 are now evidenced and
  checked. No privileged path or security pin was weakened.
- NEXT EXACT ACTION: obtain human approval for the exact elevated orphan
  process cleanup if a process-clean host is required, then stop at the
  remaining signing, clean-host, and populated-index prerequisites.

## Conflict-dialog test race (2026-08-11)

- A clean Debug/Release CTest run intermittently failed
  `fffileoperations_tests` at the custom conflict-dialog result assertion,
  although the same test passed on focused retry.
- Root cause: the test driver used `FindWindowW` on the dialog's hidden parent;
  it could set the checkbox and post the choice before `ShowConflictDialog`
  had created the child controls, leaving `applyToAllRemaining` false.
- Bounded test-only fix: the driver now waits for a visible dialog and both
  required controls before setting state and posting the choice. No production
  dialog behavior changed. Focused Debug stress passed 20/20 consecutively;
  Analyze/Release rebuilds passed; complete serial Debug and Release CTest
  passed 44/44; and `pwsh ./verify/verify.ps1 build` passed.
- The test-race fix is committed as `f862af1`; the final local checkpoint is
  this branch's clean HEAD after the follow-up workstate record.
- NEXT EXACT ACTION: stop at the documented external release blockers unless
  authorized signing material, clean-host access, or required system-level
  visual environments become available.

## Standard-state Computer Use recheck (2026-08-11)

- Read-only host state: system DPI is 96 (100% scale), High Contrast is off,
  and client-area animations are enabled. No system setting was changed.
- A fresh rebuilt Debug launch through Computer Use showed one FastFiles window,
  no startup Settings dialog or hidden modal, correct `Last Operation Errors…`
  text, `Instant search: basic (degraded mode)`, `This PC` startup, and the
  status line `Selected: 0 • 0 B • Computer`. The menu opened without text
  corruption, and `Ctrl+L` exposed a native edit control before Escape restored
  the normal chrome.
- The exact Debug UI and sibling Engine were closed/cleaned; no FastFiles
  processes remain. No new product defect was found. This strengthens the
  100%-DPI/normal-theme baseline but does not prove 150% DPI, High Contrast, or
  animations-disabled rows.
- NEXT EXACT ACTION: only execute the remaining 5.1–5.3 system visual rows or
  4.2/4.5–4.8 signed/clean-host rows when their external prerequisites exist.

## Root refresh diagnosis (2026-08-11)

- The standard Debug launch continued to show `Refreshing…` at the synthetic
  `This PC` root after 11 seconds, while the window remained responsive.
- A real address-bar navigation to the safe user Temp directory rendered its
  contents promptly and without the progress label. This classifies the root
  label as active large-root enumeration rather than a stuck refresh defect;
  no product change was justified.
- The test navigation was restored to `This PC` before shutdown, and the exact
  Debug UI/Engine processes were cleaned up.
- NEXT EXACT ACTION: preserve the current implementation and wait for the
  authorized system-level visual or signed privileged-path prerequisites.

## External prerequisite re-audit (2026-08-11)

- `pwsh ./verify/verify.ps1 doctor` confirms the Visual Studio VC toolset,
  Windows SDK, CMake, Ninja, Application Verifier, ETW/logging tools, and
  PowerShell prerequisites. Optional WPA/Procmon/WinDbg tools are absent but
  are not the release blockers.
- Read-only Authenticode inspection reports the built Debug/Release UI, Engine,
  and Index Service artifacts as `NotSigned`; `PinnedSignatures.h` still holds
  the all-zero engine/service placeholders. No certificate or thumbprint was
  invented, and no authentication bypass was introduced.
- This confirms the remaining 4.2/4.5–4.8 gates require authorized signing
  material and clean/elevated validation state, while 5.1–5.3 still require
  system-level display transitions and evidence artifacts.
- NEXT EXACT ACTION: wait for explicit authorization to change local display
  settings and for provisioned signing/clean-host prerequisites.

## Diagnostic escape-hatch build guard (2026-08-11)

- Found a concrete CMake defect while auditing the remaining authentication
  task: `src/setup/CMakeLists.txt` called `target_compile_definitions(ffsetup
  ...)` before creating the `ffsetup` target. The normal OFF path concealed the
  defect, but the intended Debug-only diagnostic configuration could not
  configure.
- Bounded fix: create/configure `ffsetup` first, then apply the option and keep
  the existing fatal guard for every non-Debug `CMAKE_BUILD_TYPE`. No runtime
  authentication behavior or fail-closed production path changed.
- Validation: an isolated Debug configure with
  `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED=ON` configured and built `ffsetup`; an
  isolated Release configure with the same option rejected configuration with
  the expected fatal error. Normal Debug `ffengine_authenticode_verification_tests`
  build/test also passed. Matching-x64 Release configure/build passed for all 51
  targets, and Release CTest passed 44/44 with the documented user TEMP/TMP
  override. The default shell's x86 library mismatch was harness-only and was
  eliminated by `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`.
- The bounded CMake fix is committed as `565b90e`. Remaining
  release blockers are the explicitly external signing/elevation/clean-host
  evidence and system-level visual matrix rows; no task is marked complete
  without that evidence.
- NEXT EXACT ACTION: obtain the provisioned signing certificate and authorized
  elevated/clean validation hosts for the remaining external gates.

## Computer-Use visual validation checkpoint (2026-08-11)

- Reconciled Git before this slice: branch `recovery/fast-files-20260809`, HEAD
  `9b10816aafa95409eb76460e860da70be0dbab18`; existing UTF-8 fix frontier in
  `CMakeLists.txt`, `tests/ui/CMakeLists.txt`, and `tests/ui/test_ui_text_encoding.cpp`
  preserved. The bounded details-pane rendering change in
  `src/ui/src/WindowShell.cpp` and the UTF-8 fix were committed as
  `93b3847` (`ui: harden text and details layout`).
- Computer Use was available and used directly against the exact rebuilt Debug
  `build/debug/src/ui/FastFiles.exe` window. Clean relaunch confirmed title
  `FastFiles`, proper `Last Operation Errors…`, proper `Selected: … • … • …`,
  degraded badge, and no startup mojibake. The direct screenshot capture does
  not persist a file; observations are recorded here from the live screenshots
  and accessibility states.
- Visual rows passed: fixture navigation through Level1–Level5; back; external
  directory watcher insertion; selection identity after a preceding external
  sort key; details pane open/closed; wide light layout; dual-pane layout with
  the details region reserved; Dark then Light theme; command palette labels;
  Settings tabs; search query `watcher` returning the fixture file; and a
  responsive UI after stopping the exact Debug Engine sibling. Storage
  Overview's native list/radio controls were exposed and exercised through the
  real UI; the canvas capture did not show those child controls, so no tile
  screenshot claim is made for degraded Treemap data.
- Concrete product defect found: `RenderDetails` used a fixed 120-DIP property
  text rectangle, so a long fixture path wrapped into later metadata. Bounded
  fix: measure the DirectWrite property layout and place the preview below the
  measured block, suppressing preview drawing when no vertical space remains.
  Rebuilt `FastFiles` successfully; focused `ffui_text_encoding_tests` and
  `ffdetails_layout_tests` passed. Rebuilt visual confirmation shows path,
  metadata, and `No preview available` separated without overlap.
- System-level rows not changed without explicit confirmation: 150% DPI,
  High Contrast, and animations-off. Treat those rows as environment-blocked
  unless the current host is already in that state. Narrow-window coverage was
  exercised through the real system resize menu; at 335/162-pixel captures the
  app collapsed/removed usable content as expected for the extreme minimum,
  then was restored to maximized state. No settings were left changed.
- Final gates: pinned OpenSpec strict validation PASS (45/45); Debug configure
  and full build PASS; Analyze configure/build PASS (the first wrapper timed out
  while the existing analyze workers continued, and the follow-up build reported
  `ninja: no work to do`); documented serial CTest PASS (44/44) with user TEMP/TMP.
  The first CTest attempt's `fffileoperations_tests` and mixed-line-ending guard
  failures were isolated, then both focused reruns passed before the final suite.
  The exact disposable visual fixture was removed and the exact Debug UI/Engine
  processes were stopped. No merge or push occurred.
- Slice handoff: no further implementation action is pending. If this work is
  resumed, re-read this checkpoint and inspect Git before selecting exactly one
  next frontier. External privileged-path and system-setting visual rows remain
  explicitly blocked; do not merge or push.

## Interactive validation product defect — Unicode literal encoding (2026-08-11)

- Direct supervised Debug observation after commit `9b10816aafa95409eb76460e860da70be0dbab18` found reproducible mojibake in the actual FastFiles window: `Last Operation Errorsâ€¦` and `Selected: 0 â€¢ 0 B â€¢ C:`. The intended glyphs are U+2026 HORIZONTAL ELLIPSIS and U+2022 BULLET.
- Root cause established: first-party UTF-8 source contains raw wide Unicode literals, but the MSVC warning-policy block does not enable `/utf-8`; the compiler therefore decodes those literals through the active legacy code page. The same issue can affect other raw UI punctuation literals, not only the two observed surfaces.
- Bounded fix frontier: enable `/utf-8` for first-party MSVC compilation and add a deterministic UI text-encoding regression test that asserts raw ellipsis/bullet literals compile to one U+2026/U+2022 code unit. Rebuild and repeat the baseline before resuming visual acceptance.

## Elevated release-validation checkpoint (2026-08-11)

- Current repository truth: branch `recovery/fast-files-20260809`, HEAD
  `feef52d0cf4bc05ff20e0cf63eaaf1fcdbb77a73`, working tree clean before this
  evidence update. No merge, push, or `main` modification occurred.
- Elevation: user `LAPTOP-8T16MFM8\\Michael Roy`; Administrator role true;
  `BUILTIN\\Administrators` enabled; `High Mandatory Level`; Windows 11 Home
  Single Language 10.0.26200 build 26200; x64. Build Tools were found under
  `C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools`.
- Gate inventory: current release tasks open are 4.2, 4.5–4.8, and 5.1–5.3
  in `close-independent-validation-gaps`; all recovery-degraded-browsing
  tasks are checked. The unrelated open backlog in
  `address-engineering-audit-findings`, `autonomous-runtime-verification`,
  and `zero-touch-autonomous-engineering` was not part of this release-gate
  slice and was not changed.
- Installer/service: idempotent upgrade PASS; uninstall PASS with service,
  task, group, metadata, install directory, and pipe absent; reinstall PASS
  and restored the pre-validation running state. Final service is
  `LocalSystem`, automatic delayed start, with query-only client-group SCM
  rights. Install-directory ACL and descriptor-builder pipe assertions pass;
  scratch is empty.
- Privilege matrix: elevated token probe PASS (integrity RID 12288); raw C:
  volume open and USN journal query PASS; Administrator control candidate
  raw-volume/USN row PASS; impostor handshake rejected PASS. Standard/non-admin
  clean-host rows remain unexecuted. No UI/Engine child processes remain.
- MFT/USN: C: is NTFS with a live USN journal; `fftest privilege`, `fsutil`
  volume/NTFS/USN queries, parser tests, and protocol fuzz tests pass. The
  authenticated engine↔service path and real privileged MFT enumeration remain
  blocked by unsigned peers and placeholder pins.
- Authentication: both configured pins remain all-zero; installed service,
  engine, and setup binaries are unsigned. Disposable untrusted-signer
  fixture was rejected by the repository's native trust helper. An expired
  certificate could be generated but Windows refused to sign with it, so no
  expired-signed result is claimed. No bypass or trusted pin was introduced.
- Repository gates: pinned OpenSpec validation 45/45; Debug PASS; Analyze
  PASS; serial CTest 43/43 PASS including `ffprotocol_fuzz_tests` and
  `ffdoc_drift_tests`. The first serial run had one transient 120-second
  timeout; focused rerun passed in 0.17 seconds and the complete rerun passed
  in 28.44 seconds.
- Interactive: visual tasks 5.1–5.3 remain blocked. The installed computer-use
  package lacked the required runtime `documentation()` API, so no
  undocumented desktop control or screenshot claim was made.
- Release verdict: `CODE COMPLETE  RELEASE VALIDATION STILL BLOCKED`.
- Evidence file: `openspec/changes/close-independent-validation-gaps/evidence/elevated-release-validation.md`.
- Next exact action: provide authorized production signing material and a
  clean supported Windows validation host, plus a desktop-control environment
  with the required guidance API; then execute only tasks 4.2, 4.5–4.8, and
  5.1–5.3. Do not alter pins before the controlled signing step.

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD at slice start: `f334b15857a6336f507c0e9ca2de1f5e742c5194` (`Record final recovery validation frontier`)
Working tree at slice start: clean
Benchmark implementation commit: `008779e` (`Add indexed search scalability benchmarks`)
Prior product optimization: `7c0cd83` (`Avoid eager candidate copies in indexed search`)
Current slice bookkeeping: SQL product commit `6d6f6c1`; Storage/details validation `3adaf7b`; authentication validation `40ce237`; recovery discovery `ab77ded`; publish failure `9d3bf9a`; watcher bound/overflow `4b8addf`

## Active Engineering Slice

Name: Final validation and blocker audit

Objective: Revalidate the complete repository gate after the overnight evidence slices, confirm security invariants and clean runtime state, and stop with only genuine external blockers recorded.

Frontier invariant: One degraded-browsing discovery/lifecycle validation slice only. Do not begin signed-service/MFT/USN validation, alter security pins, or change unrelated UI/search/SQL code. Runtime checks must use the disposable Debug build and clean up any child Engine process.

Storage/details validation slice: COMPLETE. Commit `3adaf7b` reconciles OpenSpec tasks `close-independent-validation-gaps` 1.1–1.7 and 2.1–2.6 with current source/test evidence, adds deterministic mixed-parent/rounding/overflow and narrow-layout non-overlap cases, and preserves the existing production implementation from `c6dd602`. Debug and Analyze builds passed; serial CTest passed 42/42, including `ffstorage_percentage_tests`, `ffdetails_layout_tests`, and `ffdoc_drift_tests`.

Next exact action: when authorized signing material/elevated clean-host access and interactive desktop evidence are available, execute the remaining external release gates; do not alter pins or activate the privileged path. No safe local implementation frontier remains in the current backlog.

Status-chip policy milestone: recovery task 3.1 was locally complete in source but left unchecked because its state-to-text mapping had no deterministic renderer-independent test. Commit `387228f` (`Add deterministic engine status policy coverage`) added `src/ui/src/EngineStatusPolicy.h`, routed `ColumnView::Render` through it, and registered `ffengine_status_tests` covering Starting, Connected/privileged, Connected/degraded, and Unavailable (including fail-closed privilege presentation). Focused test passed; full Debug build, Analyze build, and serial CTest passed 43/43 on 2026-08-11. Privileged runtime activation remains intentionally unclaimed because pins are placeholders.

External-blocker evidence milestone: commit `8caf002` updated `openspec/changes/close-independent-validation-gaps/evidence/documentation-and-hygiene.md` to distinguish the historical 42-test reconciliation from the current 43/43 gate and to record placeholder pins, clean process state, and unavailable signed/elevated/clean-host/interactive prerequisites. The local `openspec` executable was absent, but the pinned official package now supplies strict validation; no open external task was marked complete in that prior checkpoint.

OpenSpec validation milestone: commit `3fe729b` records `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict --no-interactive` passing all 45 changes/specs with 0 failures on 2026-08-11. This closes `close-independent-validation-gaps` task 7.2 without a global install or repository mutation; the remaining blockers are signing/elevation/clean-host/interactive only.

Pinned-authentication validation milestone: source inspection against foundation design D4 found `ConstantTimeThumbprintEqual` already implemented in `c6dd602` as a fixed 20-byte XOR/OR loop and used by `VerifyPinnedSignature`; both configured pins remain all-zero placeholders and the diagnostic unsigned escape hatch remains compile-time-only. Added `TestPartiallyPopulatedPinsRejectUnpinnedPeer` to prove a populated expected signer can pass while a placeholder expected signer rejects the same signed binary. Focused Authenticode and protocol-fuzz tests passed. The first rebuild attempt selected x86 libraries for the x64 Debug tree due to an unqualified developer-shell launch; rerun with `-Arch amd64 -HostArch amd64` linked and passed. OpenSpec tasks 4.1 and 4.3 are checked; 4.2 remains open for expired/untrusted certificate fixtures and release-pipeline evidence.

Pinned-authentication slice status: COMPLETE. Commit `40ce237` contains the deterministic partial-pin regression, task evidence reconciliation, and WORKSTATE checkpoint. Debug and Analyze builds passed; serial CTest passed 42/42, including `ffprotocol_fuzz_tests`, `ffengine_authenticode_verification_tests`, and `ffdoc_drift_tests`. No production security code or pin values changed, and the privileged path remains fail-closed.

Documentation/hygiene reconciliation milestone: commit `8caf002` records the 2026-08-11 repository search, hygiene commands, current 43-test gate, and external availability evidence. The search found only explicitly historical/superseded descriptors outside current guidance. Commit `52d33da` already deleted `dist/FastFiles.exe`, added `dist/` to `.gitignore`, and moved the 819-line `Audit.txt` prompt to `codebase-audit/Audit.txt`; the eight 2026-08-04 validation reports are unchanged. OpenSpec tasks 3.3 and 6.1–6.3 are reconciled as complete.

Final reconciliation milestone: commit `67a5d54` records the historical Debug baseline configure/build, `ctest -N`, and serial CTest result of 42/42; the later status-policy slice raised the current gate to 43/43 and Analyze also passed. Tasks 7.1, 7.2, 7.3, and 7.4 are now evidenced; external signing/elevation/clean-host/interactive gates remain open and no privileged path was activated.

Recovery discovery milestone: commit `ab77ded` records `evidence/engine-discovery.md`. The focused discovery test passed; the Debug UI launched the sibling engine from the development tree; isolated no-engine/invalid-engine probes demonstrated retry exhaustion, `engine-launch-failed` OS-error logging, and `engine-unavailable`, with all probe processes/directories cleaned. Recovery tasks 1.1–1.3 and 7.1–7.2 are complete. Recovery task 7.3 remains open because deep navigation, file opening/search, and interactive retry-click acceptance were not performed in that slice.

Recovery publish-failure milestone: commit `9d3bf9a` records `evidence/publish-failure.md`. The additive protocol value, request/watcher response paths, UI freshness mapping, oversized publication failure, stale-state recovery, blocked publication cancellation, disconnect unwind, and idle shutdown were verified by source inspection and three focused passing tests. Recovery tasks 4.1–4.3 are complete.

Recovery watcher milestone: commit `4b8addf` records `evidence/watcher-safety.md`, the 32-root LRU test, deterministic zero-byte/`ERROR_NOTIFY_ENUM_DIR` recovery and re-arm coverage, reset behavior, and the eight-overflow give-up bound. Debug and Analyze builds passed; serial CTest passed 42/42. Task 5.2 is complete.

Recovery status-visibility milestone: rebuilt Debug launch-window enumeration observed one visible top-level UI window and zero visible `FastFilesSettingsDialog` windows at startup; exact UI and Engine processes were cleaned up. Task 3.3 is now checked. Tasks 2.1–2.4 and 3.1–3.2 were subsequently evidenced by source/tests/runtime acceptance, with 3.1 now closed by the renderer-independent policy test.

Recovery status/retry milestone: the native isolated missing-engine launch reached `Unavailable` after five failed attempts; real captures showed `Starting engine…`, `Engine unavailable — click to retry`, and normal development-tree `Instant search: basic (degraded mode)`. A real chip click started a new bounded cycle (`connect_failed` 5 → 6 within 400 ms → 7 within 3 seconds) while the UI remained responsive. Tasks 2.1–2.3 and 3.2 were then evidenced; 2.4 and 7.3 were closed by later runtime acceptance. The privileged active state remains an external runtime blocker, while task 3.1's local state/text contract is now covered without activating that path.

Recovery lost-Engine milestone: commit `0805bae` records that the normal development-tree Debug UI remained responsive after the exact sibling Engine PID 36468 was terminated and reconnected a replacement Engine PID 36572 through the existing lifecycle. Cleanup verified zero remaining UI/Engine processes. Task 2.4 is now checked. The privileged active runtime state remains externally blocked by placeholder pins; scope-label/gating reconciliation (6.1) and full manual navigation/search acceptance (7.3) were later checked.

Recovery scope-authority checkpoint: `SearchPanel::RefreshScopeLabels` and the snapshot-backed `UpdateScopeAvailability` policy both originate in `c6dd602`; degraded mode intentionally keeps browsed-location scopes available while the service is disconnected. The task wording still says to retain prior engine gating, so runtime evidence must distinguish the actual labels from the unresolved specification clause before task 6.1 is closed.

Development-tree acceptance milestone: commit `8652401` records that the disposable Debug UI/Engine pair completed the five-level navigation, Alt+Left, Backspace/up, file selection/open, real Ctrl+C/Ctrl+V copy, degraded `acceptance` search, exact scope-label observation, and synthetic-root drive listing. The missing-engine/retry run in `evidence/status-retry.md` completes the acceptance chain. Tasks 6.1 and 7.3 are checked; recovery task 3.1's privileged-active runtime observation remains blocked by placeholder pins, but its local state/text contract is now separately tested.

Scope-contract reconciliation milestone: commit `feca05a` updates D6, Requirement 6.2, and task 6.1 to the source-backed rule that degraded browsed-snapshot scopes remain available with honest labels while indexed coverage is separately identified and filtered. The pre-existing invalid CP1252 bytes in `spec.md` were mechanically normalized to UTF-8/CRLF as part of the artifact edit; `ffdoc_drift_tests` passed.

Pinned-signature procedure frontier: commit `ff06a99` evidences close-independent task 4.4 with the controlled release-time leaf-thumbprint derivation, no private material in source control, single-pin rotation with restart/revalidation, and fail-closed placeholders; this slice invents no values.

Final validation milestone: commit `c906f60` was followed by a no-op Debug build, a clean Analyze build, and serial full CTest `42/42` pass under the established user TEMP/TMP environment. `git status --short` was clean before this checkpoint, no `FastFiles` or `FastFilesEngine` processes remained, both configured thumbprints remain placeholder `{}` values, and no privileged service was activated.

Terminal blocker audit: recovery task 3.1 is locally complete; only its privileged-active runtime observation is blocked by placeholder pins. Independent-validation tasks 4.2 (expired/untrusted fixtures), 4.5–4.8 (signed/elevated/real MFT-USN/privilege/clean-host), and 5.1–5.3 (interactive visual matrix) remain open. OpenSpec task 7.2 is complete through the pinned official CLI invocation. These remaining gates are recorded as unavailable rather than fabricated; after the documentation reconciliation, no further safe local implementation frontier remains in the current backlog.

Desktop-validation tooling audit: the installed `computer-use` skill was read, but its `@oai/sky` package exposes no required `documentation()` API (only control methods). No desktop action was taken without the mandatory guidance/confirmation material; interactive visual tasks remain unverified and external.

Status/retry evidence commit: `b6e401e` records the isolated native-environment launch, real Starting/Unavailable/degraded captures, five-attempt exhaustion, and a real retry-chip cycle. The subsequent lost-Engine reconnect evidence is now ready for its own focused commit.

Recovery acceptance boundary: a read-only UI Automation probe could not open Search in the restored profile because its canonical shortcut file currently contains only `app.settings`; no user configuration was changed and no search-label runtime claim was made. The recovery source already emits the browsed-location labels, while the older instant-search artifact retains superseded whole-volume gating language; do not change either behavior without a resolved authority.

Final validation checkpoint before the watcher slice: after commit `a46dfed`, `cmake --build --preset debug` completed with no work, `cmake --build --preset analyze` completed cleanly, and serial CTest passed 42/42. A new full Debug/Analyze/CTest gate after `4b8addf` also passed 42/42; the current status-chip runtime slice is not yet claimed.

Benchmark decision checkpoint: the failed-literal matcher slice is complete at `0e7ebf6` with bookkeeping at `1c2353b`; benchmark commit `c36f8bd` records the final 100K/500K/1M/2M matrix. Vendored SQLite FTS5 creation fails (`no such module: fts5`). UTF-16 trigrams had zero false negatives across 12 eligible query classes at every scale; at 2M, selective/no-match used 4/3 candidates at 0.01/0.00 ms with an estimated 168.6 MiB index. Sorted-prefix ranges missed 1,969 current-infix matches at 2M. Debug and Analyze builds pass; serial CTest passes 42/42.

Required evidence: vendored SQLite FTS5/trigram capability; canonical-match recall; candidate reduction; query medians/p90; build time; estimated and observed memory; persistent-size proxy; insert/delete/rename batches; duplicate/short/Unicode/current-infix behavior; and an explicit decision record.

Benchmark result: COMPLETE. No production code or SQLite schema changed in `c36f8bd`; the benchmark remains an informational target outside CTest. The selected integration policy is ASCII simple literal terms with at least three UTF-16 units, with short/Unicode/glob/multi-term/folder/metadata queries using the canonical linear path. The accelerator is only a candidate generator; canonical verification remains authoritative.

Integration milestone: `Projection` now owns UTF-16 trigram postings keyed to live dense slots, rebuilds them after durable-store projection rebuild/compaction, and updates them on Upsert/Remove/RemoveVolume. Allocation/update failures invalidate only the accelerator; `FilenameCandidateIndices` returns `nullopt` and `IndexPipeline::SearchIndexed` uses the existing canonical scan. Eligible view-compatible simple literals with folded length >=3 use candidate slots; all other queries are unchanged. Focused `ffindexstore_projection_tests` and `ffengine_index_pipeline_tests` pass, including selective, empty no-match, short-term fallback, folder fallback, delete, and rename coverage.

Integration reprofile: the final historical-preserving Release artifact `docs/benchmarks/large-index-scalability-secondary-index.json` covers 100K/500K/1M/2M. Selective/no-match/broad medians were `1.93/2.02/10.99`, `17.71/18.06/29.15`, `34.34/36.25/47.83`, and `64.91/67.84/81.36 ms`; at 2M, selective used one candidate and no-match used zero. The SQLite metadata query was `~65–68 ms`, now the dominant measured stage. The accelerator removes the O(N) projection scan, but SQL remains a separate follow-on slice.

Integration slice status: COMPLETE. Product commit `90efdf971f9f5be61db57102e8b857b6e8036768` contains the projection-owned trigram index, production routing, mutation/fallback tests, benchmark telemetry, and the historical-preserving 100K/500K/1M/2M Release artifact. Debug and Analyze builds passed; focused projection/engine tests passed; full serial CTest passed 42/42 after the documented line-ending normalization; `ffdoc_drift_tests` passed.

Next exact action: inspect the Store implementation and schema for `GetAllVolumes`, add only a focused timing/plan probe if existing telemetry is insufficient, and measure the exact metadata stage on the established Release harness before editing SQL.

SQL root cause: `Store::GetAllVolumes()` calls `CountEntries()` for every volume. `CountEntries()` runs `SELECT COUNT(*) FROM entries WHERE volume_id=?`; the current `(volume_id, parent_frn_low, parent_frn_high)` covering index still scans every entry for that volume. The indexed-search path only consumes `available`, `rowId`, and `VolumeKey`, so its metadata call is paying an unnecessary O(entries) count. `EXPLAIN QUERY PLAN` confirms a covering-index search for the count and a table scan for the volume list. The safe candidate is a no-entry-count overload used only by indexed search; preserve counted metadata for rebuild and unavailable-volume reporting.

SQL slice decision: implement the no-count search path first, then rerun the established Release 100K/500K/1M/2M benchmark. Do not add a schema counter or persistent migration unless measurements prove the no-count path cannot remove the dominant cost.

SQL implementation milestone: `Store::GetAllVolumesWithoutEntryCounts()` shares the existing volume-row reader while skipping `CountEntries()`. `IndexPipeline::SearchIndexed` uses it in both view-compatible and owning fallback branches; rebuild, public volume metadata, and unavailable-volume reporting retain counted `GetAllVolumes()`. Focused Store and engine tests pass.

Benchmark result: the complete `docs/benchmarks/large-index-scalability-sql-no-count.json` artifact covers 100K/500K/1M/2M with 179 runs and 28 summaries. Selective/no-match/broad medians are `0.03/0.06/8.82`, `0.50/0.62/11.89`, `0.64/0.63/14.29`, and `0.55/2.24/17.55 ms`; 2M SQLite metadata is approximately `0.10 ms`, versus `~65–68 ms` before. The 120-second external command deadline expired after the complete artifact was written; no benchmark process or disposable fixture remained.

SQL slice status: COMPLETE. Product commit `6d6f6c1` preserves counted metadata semantics while routing indexed search through the no-count API. Debug and Analyze builds passed; full serial CTest passed 42/42; the Release artifact is complete and valid.

Storage percentage milestone: existing implementation from `c6dd602` already owns the pure helper, complete-parent denominator, unknown/aggregate refresh, and division-first ratio sorting. Expanded `tests/ui/test_storage_percentage.cpp` now covers mixed/only-file/only-directory composition, rounding, root/unknown, equal and near-equal values, and `UINT64_MAX`-scale ordering; focused `ffstorage_percentage_tests` passes. OpenSpec tasks 1.1–1.6 are now checked off with source/test evidence; task 1.7 remains open pending the final gates.

Storage percentage slice status: COMPLETE. Existing implementation from `c6dd602` plus the expanded `tests/ui/test_storage_percentage.cpp` satisfies tasks 1.1–1.6; task 1.7 is checked off with Debug PASS, Analyze PASS, serial CTest PASS 42/42, focused `ffstorage_percentage_tests` PASS, and `ffdoc_drift_tests` PASS. The bounded changes are test coverage, OpenSpec task evidence, and this checkpoint; no production behavior was reworked because current source already matched the design.

Details-layout milestone: existing implementation from `c6dd602` already owns the pure DIP helper, WindowShell reservation, reduced render/hit-test viewport, dual-pane split, disclosure rail, and hidden-pane threshold. Expanded `tests/ui/test_details_layout.cpp` now covers both narrow strategies; focused `ffdetails_layout_tests` passes. OpenSpec tasks 2.1–2.6 are checked off with source/test evidence.

Next exact action: run the full Debug build, Analyze build, and serial CTest suite after the Storage/details test additions; then inspect the bounded test/task/workstate diff and commit this reconciliation slice.

Reconciliation: HEAD began at `6d6f6c1` with the SQL slice committed and only this bookkeeping update uncommitted. Storage percentage code/test seams predate this frontier in `c6dd602`; task checkboxes are stale and will be reconciled from current source/tests rather than assumed complete.

Baseline reproduction complete before editing using Release `ffengine_bench_large_index_scalability` at 100K/500K/1M. Fresh medians: 100K selective/no-match/broad/worst-substring `7.23/7.11/12.28/13.17 ms`; 500K `72.46/79.12/32.48/32.34 ms`; 1M selective/no-match/broad/worst-substring `131.06/166.33/48.83/50.92 ms`. 1M p90/max: selective `131.56/138.36 ms`, no-match `167.23/167.60 ms`, broad `48.93/49.03 ms`, worst-substring `52.19/53.27 ms`. Incremental 1M `p/ph/pho/phot/photo = 47.62/56.68/56.76/56.60/56.65 ms`. Results were `1/0/4096/4096`; variation is within the established baseline band.

Completed: traced the current literal matcher and added the narrowest independent matcher measurement seam before product editing.

Matcher trace and microbenchmark complete. The old simple-literal view path is `Query::MatchesView` -> `EvaluateSimpleLiteral` -> `OrdinalContains16Prepared`. Query-side ASCII text is folded once; ASCII-compatible names use a query-selected rare anchor with `std::u16string_view::find` followed by a full folded-view equality check. Uppercase or non-ASCII names, and non-ASCII queries, use a loop of `CompareStringOrdinal(..., TRUE)` for each possible UTF-16 window. Length-too-long candidates reject before either scan. There is no per-candidate allocation, but the ordinal fallback repeats the Win32 comparison for every window; no standard searcher or compiler SIMD path is used.

Added the isolated non-CTest Release target `ffsearch_bench_literal_matcher`, using 1M names from the existing synthetic filename distribution plus short/long/failed/late/common-prefix and Unicode datasets. Pre-change direct-matcher medians (ms) were: failed `19.1`, early `22.7`, late `26.6`, single `24.8`, 2-char `26.9`, 3-char `22.0`, 4-char `27.7`, 5-char `27.4`, medium `26.9`, long `16.1`, too-long `3.4`, common-prefix `21.3`; first+last was better for late/2-4-char cases, while BMH was `14.8/20.9/25.3/45.0/34.6/29.5/25.7/25.0/20.7/12.9/3.6/16.5` respectively. The single-character BMH regression is material; BMH is the clear general winner for failed, medium/long, and common-prefix ASCII cases. Unicode remains the exact fallback and showed no BMH eligibility.

Implemented the minimal production candidate: query-prepared 128-entry BMH shift data is stored only in `Query`; simple ASCII literal matching selects BMH for folded needles of length 4+, keeps the existing rare-anchor matcher for 1-3 characters, and retains the exact ordinal fallback when the name/query is not ASCII-eligible. Added a boundary assertion (`ph` stays anchor, `phot` selects BMH) to `ffsearch_tests`. No persistent lowercase names or auxiliary index was added.

Completed: real Release SearchIndexed benchmark, attribution run, and incremental comparison performed; the measured BMH improvement is retained as the smallest worthwhile matcher change, but no secondary index was implemented.

First post-change real wall-time run (non-diagnostic, same Release harness) measured 1M selective/no-match/broad/worst-substring `133.15/147.13/48.12/49.53 ms`, versus the reproduced `131.06/166.33/48.83/50.92 ms`. BMH materially reduced no-match by about 11.5% but did not move selective or broad; it misses the provisional `<100/<125 ms` engineering bands. The diagnostic run is attribution-only because per-row clocks perturb the full scan; it showed roughly `82 ms` matcher clock and an inflated `93 ms` projection-only floor. The current wall-time floor remains close to the combined projection plus SQLite lower bound, so avoid speculative additional algorithms.

Final multi-scale candidate artifact: selective 100K/500K/1M `6.59/58.33/126.99 ms`; no-match 1M `147.90 ms`; broad `48.82 ms`; worst-substring `49.95 ms`; extension `49.30 ms`. Final 1M p90/max: selective `129.34/130.71 ms`, no-match `149.38/152.35 ms`, broad `49.99/50.73 ms`, worst `50.13/51.20 ms`. Incremental `p/ph/pho/phot/photo = 46.97/56.26/56.38/55.62/55.70 ms`. Estimated no-match residual after the unchanged `54.28 ms` projection floor and `33.98 ms` SQLite floor is `~59.64 ms`, down from `~83.73 ms`; the remaining full scan still justifies secondary-index evaluation.

Memory: isolated 1M open/rebuild/broad/post private bytes `4.9/453.8/468.8/470.1 MiB`, with no material regression. Semantics: focused `ffsearch_tests` and full CTest cover/pass ASCII, uppercase fallback, Unicode/mixed Unicode, short/overlong, prefix, glob, extension, parent path, multi-term, duplicate, cap, and ordering behavior.

Validation: Debug PASS; Analyze PASS; final serial CTest PASS `42/42` with the documented TEMP/TMP override. Manual runtime smoke PASS for rebuilt UI launch, Engine spawn, responsiveness, graceful UI shutdown, and cleanup. The non-elevated `verify.ps1 run` wrapper hung before launching UI/Engine and was terminated; no security bypass was used. `docs/benchmarks/large-index-scalability.json` and `.md` were updated with the candidate evidence and historical progression.

Decision: `SECONDARY FILENAME INDEX EVALUATION REQUIRED`. New primary bottleneck: `full O(N) projection traversal plus residual literal rejection`. 2M: NOT TESTED because 1M selective remained above the optional scale-up gate. Product code, narrow benchmark, semantic test, benchmark artifacts, and validation are complete in focused product commit `0e7ebf6`.

Completed: reviewed staged scope with `git diff --cached --check`, `git diff --cached --stat`, and `git diff --cached`; product commit `0e7ebf6` (`Optimize failed literal filename matching`) was created. Working tree was clean immediately before the commit.

Next recommended slice (not started): `Design and benchmark a secondary filename-search index`.

## Previous Engineering Slice

Name: Optimize projection traversal and direct filename matching

Objective: Decompose and materially reduce the measured raw projection traversal plus direct UTF-16 filename matching stage while preserving canonical query semantics, ordering, zero-copy candidate behavior, lazy paths, and the 4,096-result cap.

Scope invariant: One search-performance frontier only. No SQLite replacement, speculative FTS, query-language changes, cancellation, snapshot optimization, MFT/USN activation, security/signing changes, or unrelated UI/product work belongs in this slice.

OpenSpec context: No existing OpenSpec change owns this optimization. The completed `instant-search` spec is the source context: indexed matching scans the in-memory projection with no per-search SQL query, and the UI uses a bounded result page. `openspec list --json` was attempted and the CLI is unavailable in this environment.

## Current Slice Frontier

- Phase 1 reproduction completed before edits using the existing Release harness and a temporary output file. Medians: 100K selective/no-match/broad/prefix/extension/worst-substring `19.57/19.30/14.57/22.63/13.72/16.59 ms`; 500K `143.56/142.27/36.51/46.63/35.71/39.21 ms`; 1M `288.81/306.55/60.44/70.21/57.67/61.63 ms`. Incremental 1M `p/ph/pho/phot/photo` = `56.17/70.76/70.61/70.73/70.20 ms`.
- Fresh 1M structural attribution: selective examined 1,000,000 rows, took `258.55 ms` in the combined projection/match stage, and materialized one candidate/name/result with one lazy path; no-match examined all rows and took `282.57 ms` with zero materialization; broad accepted the 4,096 cap after 13,779 examined rows and took `26.08 ms` projection traversal plus `8.08 ms` result/path materialization. SQLite metadata was `32.81–34.35 ms`.
- Phase 2 decomposition completed with narrow benchmark-only attribution for projection row iteration, NamePool access, name-view construction, query-term iteration, case-insensitive comparison, substring/glob/prefix/extension matching, metadata predicates, path-needed decisions, predicate interpretation, matched-row handling, and counters for rows/terms/comparisons/searches/folding/rejects/matches/path lookups.
- Broad is fast primarily because cap-before-sort terminates traversal at 4,096 accepted results; selective/no-match pay the full million-row scan. The final projection-only probe isolates a `54.28 ms` traversal floor; the failed no-match literal scan leaves an approximately `83.73 ms` matching/filter residual after subtracting that floor.
- Exactly one engineering frontier is active: determine and optimize this measured projection/matcher stage. Cancellation, snapshot work, SQL schema redesign, FTS, security/signing, and UI changes remain deferred.
- Required evidence is complete: result-cap termination, broad-vs-selective disparity, matcher semantics, and the semantics-preserving optimization are recorded below.

## Current Slice Implementation Checkpoint

- Root cause decomposed: broad is fast because cap-before-sort stops after 4,096 accepted rows (the 1M fixture stops at 13,779 examined rows). Selective/no-match traverse all 1,000,000 live projection rows. Before cache-local projection work, the scan floor was about `232.75 ms` with projection-only diagnostics; no-match also performed `2,872,240` ordinal case-insensitive window comparisons. SQLite remained about `33 ms`.
- Implemented projection locality: `Projection::RebuildVolumeFromStore` compacts dense `ProjectionEntry` storage into the original live hash-bucket order and remaps `idToIndex_` and `parentToChildren_` references. Search iteration order and cap-before-sort acceptance order are unchanged. A dense-order-valid flag uses the compact vector for rebuilt read-only scans; mutations fall back to direct live-slot iteration, so no persistent iteration-slot index is retained.
- Implemented common literal plan: query-side ASCII folding/rare-anchor preparation, entry-local `nameAsciiLower` metadata computed during ingestion, and a direct simple-literal view matcher with exact ordinal fallback for uppercase/non-ASCII names. Complex/multi-term/glob/filter queries retain canonical predicate dispatch. No lowercase name copies are stored.
- Added optional benchmark-only diagnostics and a projection-only probe. Counts cover projection rows, NamePool lookups, views, query/predicate terms, ordinal comparisons, extension/glob/metadata calls, rejects, path decisions/lookups, and accepted rows. Detailed per-operation timers are intentionally separate from normal latency runs because their clock calls perturb wall time.
- Disproved and removed from the final design: ASCII full-name/KMP matcher variants (no wall-time win before/after compaction), a projection index-vector cache (capped scans regressed), and NamePool-side ASCII flags (added a second per-row lookup). A non-owning visitor wrapper was neutral in measurement and remains only as a small type-erasure reduction.
- Final isolated 1M Release result after removing the persistent iteration-slot cache: selective `137.45 ms`, no-match `170.24 ms`, broad `54.38 ms`, prefix `58.04 ms`, extension `54.16 ms`, worst-substring `53.71 ms`; final full-matrix medians are `130.76/172.52/51.09 ms` for selective/no-match/broad, with result counts `1/0/4096`.

## Current Slice Baseline Reproduction

- Starting point was clean `8d68ac3`; no source/test changes were present before measurement.
- Existing Release harness reproduced the current optimized path: 100K selective `36.43 ms`, 500K selective `300.28 ms`, 1M selective `473.34 ms`, 1M no-match `475.37 ms`, and 1M broad `448.50 ms` medians.
- Warm 1M selective attribution: lightweight candidate materialization `407.09 ms`, SQLite metadata `34.27 ms`, match/filter/sort `31.08 ms`, lazy path `0.012 ms` for one path, total `472.47 ms` for the sampled run.
- Fresh 1M private memory samples: database open `5.5 MB`, selective peak `543.6 MB`, broad peak `530.7 MB`, post-query `528.3 MB`.
- Root cause inside the remaining stage is confirmed: `ProjectionEntry` exposes an interned UTF-16 `NameId`, but `SearchIndexed` calls `ToWString` for every row, allocates/copies every name into an owning `Candidate`, copies scalar metadata, and retains all candidates in a reserved vector. The projection has already paid for name storage; non-matching rows do not need another name allocation.
- Design frontier: add a non-owning UTF-16 `CandidateView`/view-aware canonical matcher and stream projection rows while the projection lock protects NamePool lifetimes; materialize an owning `Candidate` only for path evaluation survivors/results. Keep existing owning `Candidate` APIs and fallback search behavior intact.

## Current Slice Implementation Checkpoint

- `ffsearch::CandidateView` carries a non-owning UTF-16 name view plus scalar metadata; `FilterDefinition`/`Query` now retain view-aware built-in predicates alongside the existing owning predicates. Extension/kind matching uses the existing `towlower` semantics without allocating a per-row normalized string; ordinal and glob matching operate directly on the UTF-16 view.
- `ExecuteSearchViews` streams a callback-provided view source, preserves canonical scope/filter/match/sort behavior and cap-before-sort semantics, and requests ownership only after cheap view predicates pass. `SearchIndexed` holds the projection lock for the view lifetime, so NamePool references cannot dangle; custom filters without a view factory use the old owning fallback.
- Structural attribution now records projection rows examined, owning candidate objects, name copies, result objects, projection traversal, and lazy-path counts. The 1M selective run examined 1,000,000 rows and created/copied/materialized exactly 1 candidate/name/result; broad stopped at the same 4,096-result cap boundary after 13,779 eligible rows and materialized 4,096 candidates.
- First Release after measurements: 100K selective `19.09 ms`, 500K `126.67 ms`, 1M selective `292.85 ms`, 1M no-match `298.32 ms`, 1M broad `59.44 ms`; incremental 1M `p/ph/pho/phot/photo` `56.39/71.01/70.95/71.12/70.59 ms`. 1M selective attribution: projection traversal/direct matching `257.72 ms`, owning candidate/name `0.003 ms`, lazy path `0.009 ms`, SQLite `34.52 ms`; private memory samples: open `5.0 MB`, selective warm peak max about `456.9 MB`, broad peak `465.4 MB`, post-query `467.5 MB`.
- Focused `ffsearch_tests`, `ffsearch_scan_tests`, and `ffengine_index_pipeline_tests` pass, including view/owning predicate agreement and existing parent-path, extension, duplicate-name, ordering, scope, and 4,096-cap coverage.

## Current Slice Validation Checkpoint

- Final default Release benchmark is recorded in `docs/benchmarks/large-index-scalability.json`: selective 100K/500K/1M `7.40/57.29/130.76 ms`, no-match 1M `172.52 ms`, broad 1M `51.09 ms`; full classes and snapshot-capacity probes completed. Incremental `p/ph/pho/phot/photo` is `49.99/59.55/58.74/58.53/59.18 ms`.
- Final instrumented 1M selective sample: projection-only `54.28 ms`, normal combined projection/matching `94.82 ms` (about `40.54 ms` residual matching), SQLite `33.98 ms`, owning candidate/name `0.016 ms`, lazy path `0.012 ms` with one successful path. No-match combined stage was `138.01 ms`, leaving about `83.73 ms` failed-literal residual. Counts remain 1,000,000 rows and one candidate/name/result for selective. Isolated 1M private samples: open `4.4 MiB`, rebuild `454.0 MiB`, broad `470.4 MiB`, post-query `471.2 MiB`.
- Debug build PASS, Analyze build PASS, and full serial CTest PASS `42/42` with the documented user TEMP/TMP override; the first CTest attempt failed only the mixed-line-ending guard and the normalized rerun passed all tests.
- Runtime smoke PASS for rebuilt Debug FastFiles launch, automatic Engine launch, UI/Engine responsiveness, normal UI close, and cleanup. No indexed privileged fixture was available; degraded navigation remains the active runtime path and no signing/authentication bypass was used.
- Benchmark Markdown preserves the original baseline → path-materialization optimization → candidate/name-materialization optimization → projection/matcher optimization progression. The new primary bottleneck is failed direct literal filename matching/filtering; the next slice is not implemented.

Current product commit: `d0f58a7` (`Optimize indexed projection filename matching`).

## Benchmark Trace Checkpoint

Actual indexed-search path: `SearchPanel::Dispatch` -> `EngineClient::RequestIndexedSearch` -> `UiServer::HandleRequestIndexedSearch` -> `IndexPipeline::SearchIndexed` -> SQLite `GetAllVolumes` metadata lookup only -> dense rebuilt projection view traversal -> prepared simple-literal or canonical generic view matching -> lazy path/result materialization -> existing cap/sort -> `SerializeIndexedSearchResponse`/pipe -> UI indexed-completion candidate materialization.

Actual browse snapshot path: `IndexPipeline::ExportDirectorySnapshot` -> `ffprotocol::SerializeSnapshot` -> `SnapshotPublisher::Publish` inactive-slot copy and interlocked slot/generation commit -> UI mapping/header selection -> `ffprotocol::ParseSnapshot`.

Measured hypothesis confirmed: indexed search latency scales with total projection size because simple literal matching remains an O(N) scan, while the returned result count is capped at `kMaxIndexedSearchResultCount` (4096). Candidate/name/path work is now lazy and zero-copy for rejected rows. SQLite entry query time is effectively absent by design; the benchmark separately times the small metadata query.

Formal performance target: none found. Interpret numbers with clearly labeled engineering bands only (`<50 ms` immediate, `50-100 ms` excellent, `100-250 ms` responsive, `250-500 ms` noticeable, `>500 ms` poor, `>1 s` not instant).

## Completed Benchmark Slice

- Added optional, null-safe timing seams to `IndexPipeline::SearchIndexed` and `SnapshotPublisher::Publish`; default product callers preserve existing behavior.
- Added non-CTest `ffengine_bench_large_index_scalability` in `tests/engine/bench_large_index_scalability.cpp`. It uses the real SQLite/index projection/search/snapshot/publication/parser APIs, deterministic realistic synthetic rows, temporary per-scale databases, warm-run distributions, memory samples, incremental typing, and supersession inspection.
- Added machine-readable output at `docs/benchmarks/large-index-scalability.json` and concise interpretation at `docs/benchmarks/large-index-scalability.md`.
- Reproduction command: `cmake --preset release; cmake --build --preset release --target ffengine_bench_large_index_scalability; build\\release\\tests\\engine\\ffengine_bench_large_index_scalability.exe --output docs/benchmarks/large-index-scalability.json`.
- Reference environment: Release/MSVC 19.44.35228.0/CMake 3.31.6, Windows 10.0.26200, AMD Ryzen 5 7535HS (12 logical cores), 23.3 GiB RAM, Kingston OM8SEP4512Q-AA NVMe SSD, NTFS C:.
- Completed scales: 10,000; 100,000; 500,000; 1,000,000. Stopped before 2M/5M because 500K/1M already established the clear latency and memory boundary; larger scales would not change the next decision before optimization.
- Search medians (selective/broad/no-match): 10K 9.93/12.49/9.91 ms; 100K 145.07/145.41/145.19 ms; 500K 923.44/895.94/932.91 ms; 1M 2,037.15/1,983.53/2,098.81 ms. The similar times for zero, one, and 4,096-result queries prove total index size dominates returned count in the current path.
- Representative 1M selective attribution: candidate/path materialization 1,970.48 ms (96.7%); SQLite metadata 33.81 ms (1.7%); match/filter/sort 32.85 ms (1.6%); all other measured stages below 0.1% each.
- Actual snapshot export succeeded through 500K entries (57.37 MB serialized) and was rejected at 1M (115.16 MB) by the unchanged 64 MiB publication slot. Isolated capacity boundary: 556,454 rows / 67,108,794 bytes succeeds; 556,455 / 67,108,915 bytes fails with generation unchanged.
- Private bytes after rebuild/broad query/post-query: 10K 8.7/8.3/8.8 MB; 100K 51.2/53.7/53.8 MB; 500K 242.2/320.0/336.0 MB; 1M 478.4/678.6/722.5 MB. The run found no proven unbounded leak, but candidate copies create substantial scale-dependent memory pressure.
- Incremental `p -> ph -> pho -> phot -> photo` at 1M returned 4,096 rows at each step and measured 1,979.76–1,996.67 ms median per request. Engine cancellation is absent; UI generation checks discard stale completions while old engine work completes. No ordering correctness defect was found.
- Formal product latency target: none found. Engineering interpretation only: <50 ms immediate, 50–100 excellent, 100–250 responsive, 250–500 noticeable, >500 poor, >1 s not instant. Current behavior is acceptable through roughly 100K only; 500K is poor and 1M is not instant.
- Primary bottleneck: full projection candidate/path materialization in `IndexPipeline::SearchIndexed`. Optimization is required, but is explicitly deferred to one separate next slice.

## Optimization Trace Checkpoint

Root cause confirmed against source: `IndexPipeline::SearchIndexed` called `store_.GetAllVolumes()`, then `Projection::ForEachEntry`, copied every row into an `ffsearch::Candidate`, reconstructed every non-root parent path with `Projection::ReconstructPath` while holding `mutex_`, and only then parsed the query and called `ffsearch::ExecuteSearch`. `ffsearch::Candidate` owns `name` and `folder` strings, so every indexed row paid for full path traversal and allocation even when filename/metadata matching returned zero or one row.

Actual query requirements: default bare terms, glob/name, `ext`, `size`, `modified`, and `kind` predicates use filename/metadata only; `folder:` uses `Candidate::folder`; `CurrentFolder`, `CurrentFolderAndSubfolders`, and `CurrentDrive` scopes use the folder; `AllIndexedLocations` does not. Result sorting by Path/Relevance needs folders only for already-matched results. The existing matcher applies the result cap before sorting, so lazy materialization must happen before the cap is incremented but does not require a top-K redesign.

Concurrency constraint: the old implementation copied a consistent candidate/path view under the projection lock, then matched outside it. The optimization keeps lightweight candidate copying under the lock and acquires the same lock only when the lazy materializer first needs a path; this preserves safe projection access and bounds the new path work to path-evaluation survivors/results.

## Optimization Implementation Checkpoint

- `ffsearch::Query` now records `requiresFolder`, retains a parallel `predicatesWithoutFolder` set, and exposes `MatchesWithoutFolder`; `folder` key detection remains case-insensitive.
- `ffsearch::ExecuteSearchLazy` retains canonical scope/filter/match/sort/cap behavior while invoking a candidate materializer after cheap predicates and before path-dependent evaluation or result insertion.
- `IndexPipeline::SearchIndexed` parses before projection traversal, copies lightweight metadata/name candidates without folders, and reconstructs folders only for `folder:`/non-all scope survivors or final matched results. Unresolvable paths are rejected before the result cap is consumed.
- Focused deterministic engine coverage now includes parent-path-only matching, case-insensitive extension/name matching, multi-term AND semantics, nested duplicate names, path ordering, and the 4,096-result cap.

Focused Debug validation: `ffsearch_tests`, `ffsearch_scan_tests`, and `ffengine_index_pipeline_tests` PASS. The final full after run covers 10K/100K/500K/1M and all seven benchmark query classes; the instrumented 1M rerun reports one lazy path at 0.014 ms median and 411.42 ms lightweight candidate materialization.

## Optimization Completion Checkpoint

Status: COMPLETE; the focused optimization commit is recorded below.

Semantic evidence: deterministic engine tests pass for selective filename matching, no-match behavior, broad truncation at exactly 4,096, `FOLDER:` parent-path-only matching, `ext:TXT`, case-insensitive filename matching, multi-term AND semantics, nested duplicate names, path ordering, current-folder scope, and unresolved/disabled-volume boundary behavior. Existing search tests continue to pass for ranking, scope, Unicode/locale-independent matching, cancellation, and path reconstruction. The lazy matcher preserves the existing cap-before-sort behavior and rejects an unresolvable path before it consumes the cap.

Final benchmark evidence, using the committed historical baseline and the final 2026-08-10 Release rerun: selective 100K `145.07→46.00 ms` (3.15x), 500K `923.44→245.44 ms` (3.76x), 1M `2037.15→470.61 ms` (4.33x); 1M no-match `2098.81→472.99 ms` (4.44x); 1M broad `1983.53→441.56 ms` (4.49x). Instrumented 1M selective attribution: old combined candidate/path `1970.48 ms`; optimized lightweight candidate `411.42 ms`, lazy path `0.014 ms` for one result, SQLite `33.10 ms`, match/filter/sort `30.87 ms`, total `475.22 ms`. Incremental `p/ph/pho/phot/photo`: `1996.67/1990.67/1986.35/1990.34/1979.76 ms` before versus `445.45/448.34/446.03/444.62/446.84 ms` after. 1M private memory: database open `6.0→5.5 MB`, selective peak `601.3→540.4 MB`, broad peak `647.1→537.0 MB`, post-query `689.1→530.5 MB`; no material regression.

Validation: Debug build PASS; Analyze build PASS; full serial CTest PASS 42/42 with the established user TEMP/TMP override; `ffdoc_drift_tests` PASS. Runtime smoke PASS for normal Debug launch, automatic Engine launch, both processes responsive, and cleanup. Indexed UI search was not claimed because the active environment is degraded/no usable privileged fixture; deterministic engine coverage and the real Release benchmark are the authoritative indexed-search evidence.

Recorded commit: `e0f046b` (`Optimize indexed search path materialization`) before this WORKSTATE hash-record amendment.

Preliminary optimized benchmark (same machine, same harness, temporary after file): 100K selective/no-match/broad `37.71/36.27/53.83 ms`; 500K `219.32/224.10/214.16 ms`; 1M `497.81/495.24/457.28 ms`. Relative to the same-session pre-run (`131.02/131.99/132.63`, `854.48/895.78/843.30`, `2012.66/2077.76/1865.99 ms`), speedups are 3.47–4.20x. 1M incremental `p/ph/pho/phot/photo` is `448.50/451.93/468.85/460.35/458.16 ms` versus `1858.31/1852.13/1836.67/1838.40/1840.56 ms`. Representative 1M selective attribution changed from materialization 1944.98 ms + match 31.84 ms to materialization 427.21 ms + match 33.74 ms; the remaining materialization is lightweight name/metadata candidate construction plus one-result path work, not full projection path traversal. Same-session 1M private memory changed: open 5.7→5.8 MB, selective peak 610.6→542.5 MB, broad peak 647.9→536.8 MB, post-query 672.8→529.1 MB.

Validation: Debug build PASS; Analyze build PASS; Release benchmark target PASS. Full serial CTest PASS, 42/42, with `TEMP`/`TMP` set to `C:\Users\Michael Roy\AppData\Local\Temp`; the focused drift guard and all 42 tests passed.

## NEXT EXACT ACTION

Do not implement the next slice automatically. The single next recommended slice is `optimize projection iteration and direct filename matching` using measured dense live-entry traversal or a narrowly scoped exact filename prefilter. SQLite schema work and cancellation remain deferred.

## Previous Slice Record

Name: Stale snapshot / publish-failure UX

Objective: Make freshness, stale last-known-good data, and unavailable requested locations explicit without rewriting the snapshot architecture or discarding useful same-context data.

Safety / correctness invariant: A failed refresh or unreadable announced generation must never be rendered as current/empty success; a different requested context must never inherit rows from the previous context.

Non-goals: Watcher shutdown architecture, selection identity, Storage visibility, shortcuts, Treemap filtering, large-index/search performance, MFT/USN activation, signing/authentication, installer redesign, broad notification/toast framework, or visual redesign.

## Current Frontier

Trace completed for the real snapshot path. Producer: `UiServer::HandleRequestDirectory` / `OnDirectoryChanged` enumerate into `directories_`; `SnapshotPublisher::Publish` serializes into the inactive shared-memory slot and commits descriptor/slot/generation; `UiServer::RepublishAndBroadcastGeneration` sends `NewGeneration` only after a nonzero publish result. Consumer: `EngineClient::ConnectAndSubscribe` maps the section, `ReaderLoop` receives generation notifications, `WindowShell` posts `WM_APP_SNAPSHOT_UPDATED`, and `ColumnView`/`SearchPanel`/`StorageAnalysis`/`TreemapView` call `ReadSnapshot()`.

Silent-staleness edges proven: `EngineClient` discarded `NewGenerationPayload::generationId`; `ReadSnapshot()` exposed no generation and returned `nullopt` without a UI result; `ColumnView::OnSnapshotUpdated` logged read failure and returned, while `RefreshColumnFromSnapshot` returned silently on path miss; all source/request errors cleared same-context rows and displayed only a generic unavailable/error column. Request-time `PublishFailed` already exists as an additive enum and response path, but watcher-time publish failure was ignored by `OnDirectoryChanged`. New navigation creates new path-owned `Column` objects, so failed new contexts do not inherit old rows; same-context refreshes had no retained stale state.

Chosen direction: add one pure, headless `SnapshotFreshness` state seam (`Loading`, `Current`, `StaleWithError`, `Unavailable`) carried by each path-owned `Column`, with accepted/requested generation ordering and a failure reason. Retain rows only for the same existing `Column`; new context columns begin empty/loading and become unavailable on failure. Thread generation through `EngineClient`/`WindowShell` and make mapping/read failure explicit. Keep publish atomicity unchanged unless tests disprove it; add watcher publish-failure notification and focused deterministic tests.

Implementation milestone: `src/ui/include/ffui/SnapshotFreshness.h` now owns the pure state transitions and generation ordering. `EngineClient::ReaderLoop` validates/deduplicates `NewGenerationPayload`, `ReadSnapshotWithGeneration` returns the committed generation, and `WindowShell` coalesces the newest generation onto the UI thread. `ColumnView` retains same-context rows for source, publish, snapshot-load, path-missing, and disconnect failures with bounded inline status text; it clears only unavailable contexts and treats a successful zero-item snapshot as `Current`. `UiServer::OnDirectoryChanged` broadcasts watcher-time publish failure to subscribed clients. `tests/ui/test_snapshot_freshness.cpp` covers the deterministic state model; `test_snapshot_publisher.cpp` proves an oversized publication returns failure without advancing shared generation. Focused tests and full Debug build pass so far.

Runtime milestone: a rebuilt Debug UI used a disposable `build\\debug\\stale-runtime-fixture` containing `nested\\beta.txt` and `alpha.txt`. The real selection probe reported the fixture path and both rows. After moving that exact directory away and triggering F5, the probe still reported the same path and rows, while a captured real window showed `Showing previous results — refresh fa…` above the retained rows. Moving it back and refreshing restored the normal view with the same rows and no stale banner. A separate relaunch to a nonexistent disposable path reported that path with `VisibleItems=` and never exposed the fixture rows. The settings file and workspace state were restored to `This PC`/previous-session `C:\`; all FastFiles processes and created runtime artifacts were cleaned up.

File-operation finding: `ActiveSelectionPaths()` resolves live full paths from current rows; command handlers check existence for open/rename/conflict decisions, and `FileOperations::Execute` rebuilds transfer plans and creates `IShellItem` objects at operation time. No stale numeric metadata is used to execute an operation. No operation redesign is planned.

Completed from clean starting HEAD `273a871`; selection identity, Storage visibility, global shortcut routing, shortcut persistence, Treemap category-filter integration, watcher/UI publication lifecycle, and stale snapshot/publish-failure UX are complete. Git/source/tests remain authoritative over prior completion notes. Current product completion commit: `21481a5` (`Surface stale snapshot and refresh failures`).

## Completed Slice Record — Stale Snapshot / Publish-Failure UX

Root cause: the UI discarded announced generation IDs, exposed snapshot reads without generation, silently returned on mapping/path misses, and cleared same-context rows on request errors. Watcher-time `SnapshotPublisher::Publish` failure was also only logged. `SnapshotPublisher` itself already committed the inactive slot, descriptor, active slot, and generation in the correct order; failed oversized publication returns zero without advancing or announcing a generation.

Final model: `SnapshotFreshness` is authoritative per path-owned `Column`: `Loading`, `Current`, `StaleWithError`, or `Unavailable`, with a failure reason, requested generation, and last successful generation. Same-column data is retained only for the same logical path; a new path owns a new empty/loading column and cannot inherit another location's rows. A valid zero-item directory reaches `Current`.

Failure/recovery: source enumeration, watcher/request publication, announced-generation mapping/read, missing-path, and disconnect failures retain same-context rows as stale with bounded inline status text; contexts with no last-known-good data become unavailable and render an explicit reason. A later accepted generation clears the failure. Out-of-order generations are rejected. Existing file operations resolve current paths and filesystem state at execution time; stale rows remain actionable without relying on stale numeric metadata.

Protocol/publication scope: no protocol version change was needed; existing `DirectoryErrorReason::PublishFailed` now also reaches all subscribed clients for watcher refresh failure. `SnapshotPublisher` atomic publication was proven rather than rewritten.

Root cause established: `DirectoryWatcher::WatchLoop` (`src/engine/src/DirectoryWatcher.cpp:133-210`) invokes its `ChangeCallback` inline on the watcher thread. `UiServer::OnDirectoryChanged` (`src/engine/src/UiServer.cpp:128-135`) re-enumerates, updates `directories_`, and synchronously calls `RepublishAndBroadcastGeneration`. That publishes through `SnapshotPublisher::Publish` (`SnapshotPublisher.cpp:77-106`) and then holds `clientsMutex_` (`UiServer.cpp:150-157`) while `WriteUiFrame` -> `ffipc::WriteFrame` -> `WriteExact` -> synchronous `WriteFile` (`src/ipc/src/PipeFraming.cpp:20-31, 66-84`) writes the `NewGeneration` notification. `UiServer::Stop` (`UiServer.cpp:110-126`) calls `listener_.Stop()` then `watcher_.UnwatchAll()`/`StopEntry` joins the watcher before it reaches the subscribed-pipe `CancelIoEx` loop. A stalled UI reader therefore leaves the watcher in `WriteFile`, holding `clientsMutex_`, while shutdown waits forever in `StopEntry::thread.join()` and cannot reach cancellation. `directoriesMutex_` and `SnapshotPublisher::publishMutex_` are released before pipe output; `clientsMutex_` is the lock held across the blocking external call.

Lifecycle ownership facts: `DirectoryWatcher::StopEntry` sets `stopRequested`, signals `stopEvent`, cancels pending `ReadDirectoryChangesW`, and joins its `std::thread`; `PipeListener` accepts each UI connection on a detached `HandleConnection` thread; `WindowShell::Run` stops only its UI-side `EngineClient`, while the engine normally parks in `Main.cpp` and `UiServer` teardown is primarily destructor/process-lifecycle driven. Existing `CancelIoEx(pipe)` in `UiServer::Stop` was too late for the watcher chain; the final implementation moves cancellation before watcher join and adds active writer-thread cancellation.

Implementation milestone: `UiServer::ClientConnection` now owns each subscribed pipe handle, serializes only that connection's complete frames, and releases `clientsMutex_` before invoking the potentially blocking frame writer. `UiServer::Stop` snapshots and cancels subscribed connections before `DirectoryWatcher::UnwatchAll` joins watcher threads. Production cancellation uses `CancelSynchronousIo` for the active writer thread plus `CancelIoEx` on the pipe handle; connection close cancels before waiting for an in-flight writer and then performs the sole `CloseHandle`. The global cross-client write mutex and raw subscribed-handle vector were removed.

Lock-order invariant: `clientsMutex_` and `ClientConnection::stateMutex_` are never held across a pipe write. `ClientConnection::writeMutex_` intentionally serializes complete synchronous frames for one byte-stream connection and may span that write, but shutdown cancellation never acquires it: `ClientConnection::Close` marks closing, cancels the active writer, then waits for `writeMutex_`; a later writer observes closing before issuing I/O. Watcher `mutex_` remains released before `StopEntry` joins. `directoriesMutex_` remains released before `SnapshotPublisher::Publish` returns to notification output.

Deterministic tests: `tests/engine/test_ui_server_lifecycle.cpp` injects a blocking frame sink into the real `UiServer`/`DirectoryWatcher` path. It verifies normal watcher publication, blocked-sink shutdown, downstream disconnect unwind, and idle shutdown. Focused result: `ffengine_ui_server_lifecycle_tests` PASS; blocked shutdown observed 10 ms against a 500 ms deadline. Existing `ffengine_directory_watcher_tests` and `ffengine_snapshot_publisher_tests` also PASS.

Runtime evidence: rebuilt Debug `FastFilesEngine.exe` served a real .NET named-pipe client. The client observed SubscribeAck/status, initial directory publication, and a new generation after an externally-created disposable file. After the client disconnected, the Engine remained alive and responsive. The actual rebuilt Debug `FastFiles.exe` launched, exited via `WM_CLOSE`, relaunched, and exited again; no FastFiles/Engine processes or disposable runtime directories remained after cleanup. The synthetic stalled-pipe evidence remains the exact bounded-shutdown proof; no fragile large-pipe-fill claim is made.

Validation: current-slice Debug rebuild PASS; Analyze build PASS. Full serial CTest PASS, 42/42, with `TEMP`/`TMP` set to `C:\Users\Michael Roy\AppData\Local\Temp`; the new `ffsnapshot_freshness_tests` is registered and passing. Focused `ffsnapshot_freshness_tests`, `ffengine_snapshot_publisher_tests`, and `ffengine_ui_server_lifecycle_tests` PASS. Runtime stale/recovery/new-context smoke PASS as above.

NEXT EXACT ACTION: Do not implement the next slice. The single next recommended slice is `large-index search/snapshot scalability benchmarking`.

Required data-flow map:
`category-filter UI/state` -> `Storage Analysis model` -> `filtered dataset/request` -> `Treemap layout input` -> `rendered Treemap`.

Root cause established: `StorageAnalysis::categoryFilter_` is the only live filter state, and `RefreshData()` applies it only to the list's `items_` through `ApplyCategoryFilter()` (`src/ui/src/StorageAnalysis.cpp:171-188, 495-497`). The category-combo `CBN_SELCHANGE` path calls `RefreshData()` but never informs the Treemap (`:690-693`). `TreemapView::BuildTree()` (`src/ui/src/TreemapView.cpp:129-265`) reads `EngineClient::ReadSnapshot()` directly, has no `CategoryEngine` or selected-category input, and builds/layouts the unfiltered hierarchy. `ApplyModeVisibility()` starts that raw build on Treemap entry, and `OnSnapshotUpdated()` rebuilds it again, so a selected list filter is dropped on entry/re-entry and after snapshot refresh; raw folder aggregate callbacks can likewise describe unfiltered data.

Category semantics established from the existing implementation: `CategoryEngine::Match()` is the canonical case-insensitive final-extension classifier. Default selectable categories are Video, Image, Document, Archive, Executable, Development, VM Image, and Game; unmatched/no-extension files are consumed as `Other` by the existing By Category/list fallback, but `PopulateCategoryFilter()` currently exposes only All plus configured definitions. `All categories` is the unfiltered state. Existing list filtering retains directories for drillability and filters files by the classifier; the Treemap fix will apply the same classifier to files, retain only directory containers with matching descendants, and recompute their totals from retained children.

Planned data-flow fix: carry a category ID (empty = All) from the existing combo state through `StorageAnalysis` into `TreemapView`; use `CategoryEngine` for both list and Treemap matching; filter the constructed hierarchy before `RebuildLayout()`; skip unfiltered aggregate responses while a category is active; rebuild on filter changes, mode entry, and snapshot updates. The renderer remains layout/paint-only.

Persisted formats:
- Canonical `shortcuts.json`: version/bindings array; each customization is keyed by stable `commandId` and stores numeric `key` + `modifiers` (`CSA`), or `unbound`; scope is restored from current registry metadata, not persisted.
- Legacy `settings.json`: nested `shortcuts.bindings` array of `command` + human-readable `binding`; it cannot represent scope/unbound state and is not suitable as the runtime model.
- Settings JSON is `%LOCALAPPDATA%\\FastFiles\\settings.json`; the dedicated file is `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`; both currently use the same AppDirectory convention but different loaders/writers.

Canonical source: dedicated `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`, because it is the existing runtime model and preserves stable IDs, unbound state, conflict behavior, and registry-derived Global/ActiveView scope.
Runtime in-memory source: the one WindowShell-owned `ShortcutMap`, shared with CommandPalette and SettingsDialog.
Legacy source: `settings.json`'s old `shortcuts.bindings` section, migration-only.
Migration policy: if canonical `shortcuts.json` exists, it wins (including an intentionally empty bindings array); if it does not exist, parse valid legacy entries, resolve command IDs/display names against the registry, apply valid bindings deterministically, save canonical once, and ignore/log unknown or malformed entries. Missing legacy data uses registry defaults. No later startup reads legacy shortcuts once canonical exists.
Live apply policy: SettingsDialog edits a draft of the shared ShortcutMap; accepted changes replace the WindowShell map, save canonical `shortcuts.json`, and refresh the existing CommandPalette pointer immediately. Settings/general persistence remains separate and no longer carries shortcut values.

Implementation milestone: `CategoryEngine::Matches` is the shared category predicate; `StorageAnalysis::activeCategoryId_` is the authoritative UI/model state (empty = All); `TreemapView::SetCategoryFilter` carries that state into `BuildTree`; `FilterTreemapTree` prunes before `RebuildLayout` and recomputes retained folder totals. Filter changes, category-definition changes, mode entry, and snapshot updates rebuild the Treemap; filtered builds do not request or accept unfiltered folder aggregates. Product commit: `ff0533e`.

OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts remain the planning context. Source mapping, implementation, actual Settings UI edit/apply/live/restart acceptance, native-edit/global regression, Debug/Analyze builds, full CTest, and the focused product commit are complete.

## Established Facts

- `Column` owns independent `selectedIndices`, `selectionAnchor`, and `focusIndex`; `columns_` and `columns2_` provide separate pane state.
- `RefreshColumnFromSnapshot` rebuilt/sorted `column.items`, restored selection by case-insensitive name, and could leave `focusIndex` at an unrelated row when the focused item disappeared.
- `ActiveSelectionPaths`, `CurrentSelectionSummary`, command dispatch, drag start, open, rename, delete, copy, cut, move/paste, and context actions resolve paths from the active column's current selected indices.
- Degraded enumeration entries carry no stable file ID; indexed snapshot entries carry `volumeRowId`, `fileIdLow`, and `fileIdHigh` from the existing index projection.
- Indexed identity is valid only when both a nonzero volume row and nonzero FRN data are present. Degraded identity is normalized full path, compared case-insensitively with Windows ordinal comparison.
- If both sides have stable indexed identity, matching requires volume row plus FRN. If either side has stable indexed identity and the other does not, no path downgrade is allowed; path matching is used only when neither side has stable identity.
- Error/missing directory refreshes now clear items, selection, anchor, and focus.
- Search-result pending selection still uses the existing path/name navigation mechanism; the refreshed column state now uses the same identity-safe remapping once entries are materialized.
- Debug-only `WM_COPYDATA` observation reads the active path, visible row names, selected/focused identity and row, and the existing `ActiveSelectionPaths()` operation target; it has no selection-control path.

## Decisions

- decision: Add a small header-only `ffui` identity/capture/restore seam instead of a global selection subsystem.
  rationale: Reuse existing index identity fields and current path data while keeping the behavior headless and deterministic.
  consequence: `ColumnView` retains index-based rendering/storage, but indices are regenerated only after identity matching.
- decision: Preserve indexed rename continuity only when volume+FRN proves continuity.
  rationale: A same-path replacement must not inherit an old indexed selection.
  consequence: Degraded mode follows full path; indexed-to-degraded downgrade clears safely.
- decision: Keep infrastructure and product commits separate.
  rationale: The workstate protocol must survive independently from the unfinished product slice.
  consequence: Commit only `AGENTS.md` and `docs/agent/WORKSTATE.md` first.

## Implementation State

### Completed

- Root cause trace across ColumnView, snapshot projection, degraded enumeration, search selection, and operation-target resolution.
- `src/ui/include/ffui/SelectionIdentity.h` identity and remapping seam.
- `ColumnView::RefreshColumnFromSnapshot` identity capture/restore for selection, anchor, and focus.
- Safe clearing on AccessDenied, NotFound, I/O error, and directory error callbacks.
- Eight focused deterministic regression scenarios in `tests/ui/test_selection_identity.cpp`.
- CTest registration in `tests/ui/CMakeLists.txt`.
- `src/ui/include/ffui/SelectionDiagnostics.h`, `ColumnView::CurrentSelectionDiagnostics`, and `tests/ui/selection_identity_runtime_probe.cpp` provide the minimal read-only runtime observation seam.
- Storage Analysis visibility policy, HWND application point, and focused headless regression tests are complete.

### In Progress

- None. Slice complete.

### Not Started

- None for this slice.

## Files Changed In Prior Storage Slice

- `src/ui/include/ffui/StorageVisibility.h`
  - Pure Storage mode/control visibility policy.
- `src/ui/src/StorageAnalysis.h` and `src/ui/src/StorageAnalysis.cpp`
  - Single `ApplyModeVisibility` owner; refresh is data-only and reposition is geometry-only.
- `tests/ui/test_storage_visibility.cpp` and `tests/ui/CMakeLists.txt`
  - Headless mode matrix, transitions, resize-policy stability, and idempotence coverage.
- `docs/agent/WORKSTATE.md`
  - Durable frontier and validation checkpoint.

## Files Changed In Prior Shortcut Slice

- `src/protocol/include/ffprotocol/Settings.h` and `src/protocol/src/Settings.cpp`
  - Removed shortcut vectors from general Settings persistence, exposed the shared canonical shortcut path, and added one-shot legacy extraction.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Made the runtime ShortcutMap the validated canonical model, with atomic save/load, stable IDs, scope-derived bindings, and legacy import.
- `src/ui/src/SettingsDialog.h` and `src/ui/src/SettingsDialog.cpp`
  - Settings Shortcuts page now edits a draft of the live ShortcutMap and applies it on OK.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - Startup canonical load/legacy migration and save/live-apply wiring.
- `tests/commands/test_command_system.cpp` and `tests/protocol/test_protocol.cpp`
  - Persistence, malformed/unknown, scope, path, and legacy migration coverage.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Central native-edit eligibility policy; `file.undo` is ActiveView so Ctrl+Z remains native while editing.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - GetFocus/message-target native-edit routing; Global dispatch runs before child-dialog routing and consumes once.
- `src/ui/src/NavigationChrome.h` and `src/ui/src/NavigationChrome.cpp`
  - Focus-loss cancellation no longer steals focus from a Global command's destination edit.
- `tests/ui/test_shortcut_routing.cpp` and `tests/ui/CMakeLists.txt`
  - Headless actual-binding matrix, edit suppression, non-edit regression, unknown chord, and no-double-route coverage.
- `docs/agent/WORKSTATE.md`
  - Durable routing root cause, validation, and completion checkpoint.

## Files Changed In Current Treemap Slice

- `src/ui/src/CategoryEngine.{h,cpp}`
  - Canonical category-ID match predicate shared by list and Treemap filtering.
- `src/ui/src/StorageAnalysis.{h,cpp}`
  - Owns active category ID, preserves it across mode changes, and forwards it to Treemap; list filtering uses the same predicate.
- `src/ui/src/TreemapFilter.h`
  - Pure pre-layout hierarchy filter that prunes non-matching files and recomputes retained folder totals.
- `src/ui/src/TreemapView.{h,cpp}`
  - Consumes the active category, filters before layout, recursively materializes snapshot hierarchy, and ignores unfiltered aggregate requests while filtered.
- `tests/ui/test_treemap_filter.cpp` and `tests/ui/CMakeLists.txt`
  - Deterministic All/category/empty/nested/aggregate/transition/classifier coverage and CTest registration.

## Tests Added / Modified

- `ffstorage_visibility_tests`
  purpose: Covers all six modes, active-mode/exclusive-control visibility, requested transitions, resize stability, and idempotence.
  status: PASS (focused and full CTest).
- `ffshortcut_routing_tests`
  purpose: Covers Global bindings from native edit focus, native Ctrl+C/V/X/A/Z ownership, view-local suppression, non-edit routing, unknown chords, and one-dispatch behavior.
  status: PASS (focused).
- `fftreemap_filter_tests`
  purpose: Covers full data, Image/Video filtering, nested container retention, filtered aggregates, empty results, classifier reuse, and repeated transitions.
  status: PASS (focused).

## Validation

Debug: PASS; `cmake --build --preset debug` rebuilt the current worktree and linked `build/debug/src/ui/FastFiles.exe`.
Analyze: PASS; `cmake --build --preset analyze` completed cleanly with `/analyze`.
CTest: PASS, 39/39 serial with process-local `TEMP=C:\Users\Michael Roy\AppData\Local\Temp` and `TMP=C:\Users\Michael Roy\AppData\Local\Temp`; the first run's only failure was corrected documented mixed line endings, and the final run passed all 39.
Focused persistence/routing: PASS — `ffprotocol_tests`, `ffcommand_tests`, and `ffshortcut_routing_tests`; tests cover missing defaults, save/reload, old binding removal, Global/ActiveView scope retention, malformed canonical fallback, unknown commands, legacy import, invalid chords, and settings path/source separation.
Focused Treemap/filter: PASS — `fftreemap_filter_tests`, `fftreemap_layout_tests`, and `ffstorage_visibility_tests`; the pure filter target builds and all three tests pass. The fixture proves All (775 bytes), Image (125 bytes with nested 25-byte folder), Video (400 bytes), empty Game (0 bytes), classifier reuse, and deterministic All → Image → Video → All rebuilds.
Current-slice CTest: PASS, 40/40 serial with the established long-form user TEMP/TMP override. The first post-change run's only failure was mixed line endings in touched files; normalization fixed it and the final run passed all 40.
Runtime (Treemap integration/smoke): PASS for legitimate degraded-mode shell/model coverage. Rebuilt `build/debug/src/ui/FastFiles.exe` launched with `FastFilesEngine`; Storage Analysis opened with the expected partial-coverage status and 29 browsed items. The existing category combo was driven through the owner command path, Treemap mode activated, and Treemap mode kept list/filter/status controls hidden. Treemap ↔ Largest Files plus a 1500x900 resize preserved the visibility policy; `IsHungAppWindow` was false and the Engine remained alive. Data totals/nodes were accepted from `fftreemap_filter_tests` because the current UI has no read-only Treemap-node diagnostic and degraded runtime has no trustworthy visual tile observation seam.
Runtime (Settings persistence acceptance): PASS — actual FastFiles Settings HWND `FastFilesSettingsDialog` opened via physical `Ctrl+,`; Shortcuts tab selected by native mouse plus five Right-arrow inputs; stock `SysListView32` row 24 (`Search`) entered its temporary native `EDIT` through physical double-click. Literal `F6` was observed in that editor via cross-process `GetWindowText`, committed with Enter, and applied through the real OK button control path. Canonical file contained `search.focus` key 117 with no Ctrl+F binding; legacy `settings.json` shortcut section was removed. Actual F6 focused Search query EDIT live. After Ctrl+L focused address EDIT `20776946`, old Ctrl+F left address focus unchanged. A normal close/relaunch loaded the same F6 binding; fresh Search query EDIT `3999782` focused on F6, and fresh address EDIT `133296` retained focus after old Ctrl+F.
Runtime (native-edit/global regression): PASS — from the native address EDIT, F6 focused Search; Ctrl+L focused the address EDIT; Ctrl+Shift+P opened command-palette EDIT `723122` plus ListBox `329884`, Escape closed it; F5 while address EDIT owned focus left it focused. Disposable `probe` text exercised native Ctrl+A/C/X/V/Z: selection result `327680` (0..5), copied clipboard `probe`, cut length 0, paste length 5, undo length 5. Original Unicode clipboard text was restored.
Runtime (shortcut slice): PASS on the rebuilt current Debug UI via direct Win32 `keybd_event` and live HWND state. `Ctrl+L` focused address `EDIT` HWND 264124. Native acceptance used actual typed `probe` text and `WM_GETTEXTLENGTH`/`EM_GETSEL`: Ctrl+A selected all (0..5), Ctrl+C yielded clipboard `probe`, Ctrl+X reduced edit length to 0, Ctrl+V with `PasteProbe` produced length 10, and Ctrl+Z restored length 5; focus remained the address edit throughout. Global acceptance from the edit passed: Ctrl+Shift+P focused the command-palette `EDIT` HWND 133068 with its ListBox visible, Ctrl+F focused the search `EDIT` HWND 133076, and Ctrl+L returned focus to address `EDIT` HWND 264124. F5 while address edit focus remained on HWND 264124, with no view-routing UI change observed. Escape cleanup completed separately. Storage runtime evidence below is from the completed prior slice.
Runtime (shortcut regression): PASS on a fresh current Debug GUI instance. Real address-edit paste/Enter navigation reached `C:\Windows\Temp` and `C:\Windows`; after each commit the edit hid and focus returned to the main HWND. Ctrl+Shift+P from normal view focus opened the palette `EDIT`/ListBox and Escape closed it; `IsHungAppWindow` reported responsive. No destructive file operation was used.
Runtime (prior Storage slice): PASS. Live Debug FastFiles accepted Overview, Drill Down, Largest Folders, Largest Files, By Category, and Treemap. For each mode, `IsWindowVisible`/`GetWindowRect` was queried before and after a 1500x900 resize; hidden mode-owned HWNDs remained hidden, radio state tracked the active mode, and closing Storage hid every Storage child. The Direct2D Treemap surface had no layout in degraded coverage, so its screenshot was not treated as visual tile evidence; native control visibility and checked Treemap mode were verified.
Other: the local OpenSpec executable is absent, but the pinned `@fission-ai/openspec@1.8.0` invocation passed strict validation for all 45 changes/specs; checked-in `storage-analysis` artifacts were read manually. Build tools are available under the installed VS 2022 Build Tools instance.
Prior product commit: `412e25b` (`Fix Storage Analysis mode visibility`). Prior shortcut-routing product commit: `9db73c1fed5c8ed3a96c95e9ef89b463254ce197` (`Preserve global shortcuts while editing text`). Prior shortcut-persistence product commit: `3ec28db` (`Unify shortcut settings persistence`). Current Treemap category-filter product commit: `ff0533e` (`Apply category filters to Treemap data`).

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.
- `cmake` was initially absent from the plain shell PATH; using `Launch-VsDevShell.ps1` from the installed Build Tools instance resolved the harness setup without changing the repository.
- Runtime probe: a read-only PowerShell/PInvoke query used the live main HWND and Storage child control IDs; normal command-palette and radio-button commands drove mode transitions. FastFiles remained responsive and the Engine/UI process pair stayed alive.
- Treemap screenshot capture was limited by the existing degraded empty-layout path (`TreemapView::Render` returns before clearing when `layout_` is empty); no Treemap rendering claim is made from that capture.
- Full visual Treemap tile acceptance remains unavailable: the current UI has no read-only diagnostic exposing `TreemapView::root_`/`layout_`, and degraded coverage does not provide trustworthy visual tile evidence. Pure model/data acceptance is complete; no screenshot or visual tile claim is fabricated.
- A synchronous synthetic owner-data list double-click blocked its caller during runtime probing, so that method was abandoned; the UI remained responsive and the asynchronous PostMessage path was used for smoke navigation. No product behavior changed.
- The first UI driver pass targeted row 22 (`Toggle Details Pane`) because the registry has 15 pre-Search rows, not 14; it added only an unbound customization for a command with no default. This test-profile state was removed by restoring the backed-up user files; no product source changed.
- Win32 runtime driver required a foreground handoff because each shell invocation left the terminal foreground; `GetForegroundWindow` + returned thread IDs from `GetWindowThreadProcessId` + `AttachThreadInput`/`SetForegroundWindow` made the product interactive. Cross-process pointer messages (`TCM_GETITEMRECT`) were abandoned after a timeout; no unsafe target-buffer observation was used.
- FastFiles UI closed through the native title-bar close region on each acceptance run; the exact Engine child remained orphaned after its UI parent exited and was stopped by PID after parent exit. This is runtime-process cleanup evidence, not a shortcut persistence failure.
- Plain-shell `cmake` is unavailable; focused validation succeeded after launching the installed VS 2022 Build Tools developer shell, as documented above.
- The local OpenSpec executable was initially unavailable; the pinned official package invocation later passed strict validation for all 45 changes/specs. Checked-in OpenSpec artifacts remained the planning context and no unrelated change artifacts were edited.
- The first runtime probe used a stale staged executable and invalid cross-process `GetWindowText`/`EM_GETSEL` observations; those results are explicitly excluded. The rebuilt run used the source-matching executable, actual key-generated text, `WM_GETTEXTLENGTH`, `EM_GETSEL` return values, and clipboard contents.
- Full CTest environment evidence: default 8.3 user TEMP passed `ffindexstore_store_tests` but failed unrelated `fffileoperations_e2e_tests`; `C:\Windows\Temp` passed the file-operations test but aborted the index-store test in a Debug CRT dialog; long-form user TEMP/TMP passed both isolated tests and is the selected full-suite environment.
- Runtime Treemap probe: a synchronous synthetic owner-data list double-click blocked the caller, so that method was abandoned; the UI remained responsive and the asynchronous PostMessage path was used for smoke navigation. No product behavior was changed by the probe.
- Full visual Treemap tile acceptance remains unavailable: the app is degraded/partial and no existing read-only diagnostic exposes `TreemapView::root_`/`layout_`; no screenshot or visual tile claim is fabricated. Pure model/data acceptance is complete.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.
- Storage-specific limitation retained: degraded mode may have no Treemap layout, so visual Treemap tiles were not claimed by this slice; allocated-size metadata remains the existing projection gap.

## Runtime Acceptance Checkpoint

- Settings control facts: top-level `FastFilesSettingsDialog`; tab index 5 is Shortcuts; shortcut rows are a native `SysListView32`; `NM_DBLCLK` on subitem 1 creates a temporary native `EDIT`; Enter/blur commits and Escape cancels; IDOK runs `FinishShortcutEdit`, `SaveCurrentPage`, and the real `onChanged_` callback.
- Test binding: `Search` old `Ctrl+F`, test `F6`. Search is registry row 24 (file operations 9 + navigation/selection 15).
- Acceptance evidence: canonical `shortcuts.json` has `{"commandId":"search.focus","key":117,"modifiers":""}` and no `Ctrl+F`; physical F6 focused Search query EDIT `5441536`; physical Ctrl+L focused address EDIT `20776946`; physical old Ctrl+F left address EDIT focus unchanged.
- Completed: normal close, relaunch without rewriting config, F6/old Ctrl+F restart checks, native-edit/global regression, exact user-profile restoration, generated canonical cleanup, temporary-directory removal, final Debug/Analyze builds, full serial CTest, and the focused product commit.

## Historical Prior-Slice Note

The prior slice recommended `large-index search/snapshot scalability benchmarking`; that recommendation is now completed by the current slice record above.

## Projection / filename optimization checkpoint

Status: COMPLETE; product commit `d0f58a7` is recorded below.

Root cause: broad was fast because the existing cap-before-sort behavior stopped at 4,096 accepted rows after 13,779 examined rows. Selective and no-match traversed all 1,000,000 rows. The old combined projection/direct-match stage was about 271.79 ms. Diagnostic timers overlapped, but showed roughly 102.6 ms NamePool lookup timing, 23.2 ms view construction, and 63.7 ms direct predicate timing; no-match performed 2,872,240 ordinal comparison windows.

Implementation: rebuild-time `Projection` compaction preserves exact live hash-bucket order while making entries dense; `idToIndex_` and `parentToChildren_` indices are remapped. Dense scans are used only while that order is valid; mutation paths use direct live-slot iteration. Query-side simple literal plans prepare ASCII folding and a rare anchor once. Entries carry only an ASCII-lowercase-compatible flag; the canonical ordinal/Unicode fallback remains. No lowercase name copies, cancellation, SQL redesign, snapshot change, or UI change was added.

Fresh final Release full benchmark: selective `100K/500K/1M = 7.40/57.29/130.76 ms`; no-match 1M `172.52 ms`; broad `51.09 ms`; worst substring `52.87 ms`; extension `51.74 ms`. Incremental `p/ph/pho/phot/photo = 49.99/59.55/58.74/58.53/59.18 ms`. Isolated clean 1M memory: open/rebuild/broad/post `4.4/454.0/470.4/471.2 MiB`; no material regression. Projection-only diagnostic floor `54.28 ms`; normal selective combined stage `~94.82 ms`, leaving `~40.54 ms` direct-match residual; no-match combined `138.01 ms`, leaving `~83.73 ms` failed-literal residual.

Validation: Debug PASS; Analyze PASS; serial CTest PASS 42/42 with the documented TEMP/TMP override; runtime smoke PASS for UI launch, Engine companion, responsiveness, normal UI close, and explicit cleanup. The canonical Release artifact includes 10K/100K/500K/1M and all seven query classes. Do not begin the next filename-index/cancellation slice.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
