# Code Index

A file-and-type-level map of the FastFiles source tree, derived from the
actual headers, CMakeLists, and entry points. For architecture rationale and
developer commands see `AGENTS.md`; for prose overview see `README.md`. This
file documents *what is where and how it wires together*.

Naming rule: each `src/<name>/` is one CMake target (or a small group). Public
headers live in `src/<name>/include/ff<name>/`; implementations in
`src/<name>/src/`. UI/engine headers that are internal to their own executable
live directly in `src/<name>/src/` (no `include/` dir) — those are not public.

## Targets and dependencies

Static libraries (no `main`):

| Target | Dir | Links (PUBLIC) |
| --- | --- | --- |
| `ffprotocol` | `src/protocol` | — (depends on nothing) |
| `ffipc` | `src/ipc` | `ffprotocol` |
| `ffsetup` | `src/setup` | advapi32 netapi32 ole32 oleaut32 wintrust crypt32 taskschd comsuppw |
| `ffindexstore` | `src/indexstore` | `sqlite3` (vendored) |
| `ffsearch` | `src/search` | `ffprotocol` |
| `ffmftparser` | `src/indexsvc` (lib only) | — |
| `ffindexsvcprobes` | `src/indexsvc` (lib only) | `ffprotocol` `ffsetup` |

Executables (have a `main`):

| Target | Dir | Type | Links (PRIVATE) | Entry |
| --- | --- | --- | --- | --- |
| `FastFilesIndexSvc` | `src/indexsvc` | console/service | `ffprotocol ffipc ffsetup ffmftparser ffindexsvcprobes` | `src/Main.cpp` |
| `FastFilesEngine` | `src/engine` | console | `ffprotocol ffipc ffsetup ffindexstore Rpcrt4 Shell32 Ole32` | `src/Main.cpp` |
| `FastFiles` | `src/ui` | `WIN32` GUI | `ffprotocol ffipc ffsetup ffsearch` + d3d11 dxgi d2d1 dwrite dcomp windowscodecs shell32 ole32 comctl32 | `src/Main.cpp` |
| `FastFilesSetup` | `src/installer` | console | `ffsetup wer` | `src/Main.cpp` |
| `fftest` | `src/fftest` | console (probe) | `ffprotocol ffipc ffsetup ffindexsvcprobes` | `src/Main.cpp` |

Dependency layering (arrows = "depends on"):
```
ffprotocol ◀── ffipc ◀── FastFilesIndexSvc
              ◀── ffsetup ◀── ffindexsvcprobes ◀── FastFilesIndexSvc
                            ◀── FastFilesSetup, fftest, FastFilesEngine, FastFiles

ffindexstore (sqlite3) ◀── FastFilesEngine
ffsearch (ffprotocol) ◀── FastFiles
ffmftparser ◀── FastFilesIndexSvc
```

`ffprotocol` is the leaf everything depends on; `ffindexstore` is intentionally
*not* depended on by the UI (the UI reads the index only via the shared-memory
snapshot — see Data flow).

## `ffprotocol` — wire protocol (elevation seam + UI control seam + snapshot)

Public headers in `src/protocol/include/ffprotocol/`. Namespace `ffprotocol`.

- `Frame.h` — `FrameHeader` (8 bytes: totalLength/structVersion/messageType),
  `kMaxFrameSize` (1 MiB), `IsFrameLengthValid()`. The one protocol-wide size
  bound, checked before any allocation in `u64`/`size_t` arithmetic.
- `Commands.h` — the closed engine↔service `MessageType` enum (Handshake …
  Heartbeat plus the index-storage-and-scanning additions: ScanRecordBatch,
  ScanComplete, UsnJournalOpened, JournalRecordBatch, JournalResumeInvalid);
  opaque connection-scoped `VolumeId`/`JournalId`; packed request/payload
  structs. **No message accepts an arbitrary client path/handle.**
- `Records.h` — `MftRecordFixedV1`/`UsnDeltaFixedV1` (only
  $STANDARD_INFORMATION + $FILE_NAME — never $DATA), `ParseMftBatch`/
  `ParseUsnDeltaBatch`/`Serialize*` with reject-the-whole-batch semantics,
  `IsFileNameLengthValid`, `IsBatchCountPlausible`, `kMaxBatchRecordCount`.
- `Dispatch.h` — `kCurrentStructVersion`, `ValidateFrame()` (bounds-checked
  message-type lookup; no raw jump table).
- `UiProtocol.h` — the closed engine↔UI `UiMessageType` enum (Subscribe,
  RequestDirectory, NewGeneration, EngineStatus, ReloadIndexingConfig,
  Request/Unavailable/Forget/ForgetResult volumes, Request/FolderAggregate),
  `ValidateUiFrame()`, path-length and status validators.
- `SnapshotFormat.h` — layout of the memory-mapped snapshot:
  `SnapshotSharedHeader` (generation/activeSlot/activeSlotDataSize) + two
  equal slots of `kSnapshotSlotCapacityBytes` (8 MiB); `SnapshotDirectory`/
  `SnapshotDirectoryEntry`; `SerializeSnapshot`/`ParseSnapshot`. Same-tree
  private format, not a versioned wire protocol.
- `Version.h` — `ProtocolVersion` (major/minor) + negotiation.
- `IndexHealth.h`, `Settings.h`, `Diagnostics.h` — index-health reporting,
  settings (settings.json schema, `VolumeSetting`), and diagnostics payloads.

## `ffipc` — named-pipe framing/listener

Public headers in `src/ipc/include/ffipc/`. Namespace `ffipc`.

- `PipeFraming.h` — `ReceivedFrame{header, payload}`, `ReadFrame()` /
  `WriteFrame()` over synchronous pipe handles. Rejects oversized/wrong-version
  headers *before allocating a payload buffer*.
- `PipeListener.h` — `PipeListener`: one pipe's accept loop, a fresh instance
  per connection (`FILE_FLAG_FIRST_PIPE_INSTANCE` on the first only, so a
  squatted name fails loudly), caller-supplied `SECURITY_ATTRIBUTES` (each
  caller owns its own ACL — service uses the client group, engine uses the
  current user). Reused by both the service and the engine.

## `ffindexstore` — durable SQLite store + in-memory projection

Public headers in `src/indexstore/include/ffindexstore/`. Namespace
`ffindexstore`. Links `sqlite3`.

- `Identity.h` — `FileId` (128-bit: low/high, NTFS uses high==0), `VolumeKey`
  (GUID + serial, the *durable* cross-restart identity, distinct from the
  wire's ephemeral `ffprotocol::VolumeId`), `VolumeRowId` (dense SQLite row id),
  `EntryKey` (volume + FRN — the only entry identity, never a path).
- `EntryRecord.h` — `EntryRecord` (one entry's persisted fields), `EntryChange`
  (Upsert/Remove batch unit).
- `Store.h` — `class Store`: single-file SQLite DB in WAL mode. `Open`
  (integrity-checks first, sets `outIntegrityFailed` so the caller can fall
  back to a fresh scan), `ApplyBatch` (one explicit transaction, atomic),
  `ForEachEntry` (streaming), volume-metadata CRUD (`GetOrCreateVolume`,
  `SetJournalPosition`, `SetScanCursor`, `ForgetVolume` …), `Checkpoint*`
  (passive + size-triggered forced), `GetFolderAggregate` (recursive CTE
  subtree size/count). **Not internally synchronized beyond SQLite WAL.**
- `Projection.h` — `class Projection` + `ProjectionEntry`: the RAM-resident
  view rebuilt from `Store` at startup and kept incrementally in sync. Parent
  keyed by FRN (not path); name by interned `NameId`. `Upsert`/`Remove`/
  `RemoveVolume`, `Find`, `ChildIndices` (returns `std::span`),
  `ReconstructPath` (walks parent refs + defensive cycle detection),
  `RebuildVolumeFromStore`, `GetFolderAggregate` (in-memory subtree walk),
  `ForEachEntry`. **Not internally synchronized** — the engine's single
  ingestion thread (or a startup-rebuild lock) serializes mutation.
- `FlatHashMap.h` — `FlatHashMap<Key,Value,Hash>` (open-addressing, pow-2
  sizing, linear probing, tombstones) and `FlatChildrenMap` (EntryKey →
  contiguous child-index slices, no per-key heap allocation). Used by
  `Projection` for `idToIndex_` and `parentToChildren_`.
- `NamePool.h` — `class NamePool`: deduplicated interned UTF-16 arena +
  offset/length table; identical names anywhere in the tree share one entry.

## `ffsearch` — query parsing + search + history

Public headers in `src/search/include/ffsearch/`. Namespace `ffsearch`. Links
`ffprotocol`.

- `Query.h` — `Candidate` (name/folder/size/times/isDirectory/volumeId/id/
  parentId), `FilterRegistry`/`FilterDefinition` (extensible size/date
  filters), `Query` (`predicates`, `Matches`), `ParseQuery`,
  `OrdinalContains`, `GlobMatches`.
- `Search.h` — `SearchScope`, `MatchTier` (Exact/Prefix/Substring/PathOnly),
  `SortField`, `SearchRequest`/`SearchResponse`, `ExecuteSearch` (chunked,
  cancellable), `SortResults`, `ClassifyMatch`, `ReconstructPath`.
- `History.h` — `class SearchHistory`: load/save/clear/record to disk.

## `ffsetup` — privileged install-time operations

Public headers in `src/setup/include/ffsetup/`. Namespace `ffsetup`.

- `Identifiers.h` — the single source of truth for names shared across all
  three processes + installer: service name (`FastFilesIndexSvc`), virtual
  account name (`NT SERVICE\FastFilesIndexSvc`) kept for the non-production
  registration path (the production model runs the service under LocalSystem
  — see `src/installer/src/InstallSteps.cpp`), client group (`FastFilesUsers`),
  pipe names (`kCtrlPipeName` machine-wide;
  `kUiCtrlPipeNameFormat` per-session `%u`), snapshot section name
  (`Local\FastFiles.IndexSnapshot.%u`), scheduled-task name, exe names used
  for image-path verification.
- `ServiceRegistration.h`, `GroupSetup.h`, `InstallDirAcl.h`,
  `SecurityDescriptors.h` (`OwnedSecurityDescriptor`), `ScheduledTaskRegistration.h`,
  `AuthenticodeVerification.h` (`PinnedSignatures`), `SetupResult.h` — the
  privileged operations the installer/service use to register, ACL, and
  pin signatures.

## `ffindexsvcprobes` & `ffmftparser` — service internals split out for testing

Internal headers live in `src/indexsvc/src/` (no public `include/`). The
service's CMakeLists deliberately splits the Win32-free parts into their own
static libs so they can be unit-tested without the Windows-only service exe.

- `ffmftparser` — `MftParser.cpp` only: raw MFT record byte-parsing with no
  DeviceIoControl dependency. Tested by `ffmftparser_tests`.
- `ffindexsvcprobes` — `PrivilegeVerification.cpp`, `VolumeEnumeration.cpp`,
  `ClientAuthentication.cpp`: the privilege/identity/auth probes. Tested by
  `ffprivilege_diagnostics_tests`.
- `FastFilesIndexSvc` (exe) — `Main.cpp` (SCM service entry, `SERVICE_STATUS`),
  `ServiceConnection` (per-connection handler, the handshake + mutual-auth +
  closed-command dispatch), `ConnectionRegistry` (connection-scoped handle
  tracking), `VolumeScanner`, `UsnJournalReader`, `StalenessMonitor`
  (self-directed binary-hash staleness check + SCM failure-action restart),
  `DllHardening` (`SetDefaultDllDirectories`), `DiagnosticsHardening`.
  `ServiceConnection` dispatches StartVolumeScan/OpenUsnJournal to real
  `VolumeScanner`/`UsnJournalReader` worker threads; the privileged path is
  inert in practice only because the Authenticode pins in
  `src/setup/include/ffsetup/PinnedSignatures.h` are all-zero placeholders —
  mutual auth fails closed, so degraded mode remains the active path.

## `FastFilesEngine` — unprivileged per-session index owner

Internal headers in `src/engine/src/`. Namespace `ffengine`. Entry
`src/engine/src/Main.cpp`.

- `IndexPipeline` — owns `Store` + `Projection` and is the *single* point that
  enforces "commit to durable store BEFORE applying to projection" (design D4).
  Internally synchronized (one mutex). `ApplyMftBatch`/`ApplyUsnBatch`,
  `RebuildAll`, reconciliation pass (`Begin`/`Finish`/`IsActive` — removes
  entries a full scan never observed), `ExportDirectorySnapshot` (→ snapshot
  format), `GetFolderAggregate`, `RunStoreMaintenance` (WAL checkpointing).
- `VolumeSessionManager` — the orchestrator: turns "connection Active" into
  real scan/journal/reconcile. Maps ephemeral `ffprotocol::VolumeId` → durable
  `VolumeRowId`, decides Start-vs-Resume-vs-Reconcile per volume (design D6),
  runs the periodic reconciliation scheduler (available volumes only — never
  while degraded). Wires itself as `PrivilegedConnection`'s scan/journal
  callback target.
- `PrivilegedConnection` — the engine↔service connection as a state machine
  (`Disconnected/Connecting/Handshaking/Active`, `UnavailableReason`).
  Background lifecycle thread with heartbeat + backoff; reader thread
  dispatches scan/journal frames to callbacks. `SendRequest` (thread-safe vs.
  heartbeat), `DropForIdle`/`RequestReconnect` (idle lifecycle).
- `SnapshotPublisher` — publishes the double-buffered memory-mapped section
  (`Local\FastFiles.IndexSnapshot.<sessionId>`); `Publish` writes the inactive
  slot then atomically flips `activeSlot`/`generation`.
- `UiServer` — same-privilege control-plane server for the UI. Handles
  Subscribe/RequestDirectory, republishes on watched-directory changes, pushes
  NewGeneration/EngineStatus, services unavailable-volume + folder-aggregate
  requests. Owns a `DirectoryWatcher` + `SnapshotPublisher`.
- `DegradedModeEnumerator` — unprivileged `FindFirstFileEx` enumeration
  (the permanent fallback path); inaccessible subfolders returned with
  `accessible=false` rather than aborting.
- `DirectoryWatcher` — one `ReadDirectoryChangesW` watch per browsed/pinned
  path (degraded mode); `onChanged` triggers re-enumerate + republish.
- `IdleManager` — drops the privileged connection after idle; reconnects on
  UI launch / activity burst.
- `VolumeIdentity` — drive-letter ↔ durable `VolumeKey` resolution.

## `FastFiles` (UI) — Direct2D/DirectComposition Column View shell

Internal headers in `src/ui/src/`. Namespace `ffui`. `WIN32` GUI target. Entry
`src/ui/src/Main.cpp`. Reads the index only through the mapped snapshot (zero
IPC per keystroke) + the control pipe — does **not** link `ffindexstore`.

- `UITheme.h`, `UiStyle.h`, `UiAnimation.h`, `IconCache.h` — the design-token
  system every UI surface must use: `UITheme.h` (`GetUiTheme(bool dark)` +
  `UiMetrics`, `ToColorRef`/`ToD2DColor` for GDI chrome, `gUiDarkTheme`,
  `UiSystemHighContrast()` gating overlays) is the single token set;
  `UiStyle.h` provides rounded-rect fill/stroke and solid-brush helpers;
  `UiAnimation.h` serves `SystemAnimationsEnabled()` + `FloatAnimation`
  ease-out lerp (snap-instant when animations are off); `IconCache.h` is the
  bounded, DPI-aware, off-thread icon cache. No hardcoded `RGB`/`GetSysColor`
  literals except High-Contrast fallbacks.
- `WindowShell` — HWND creation, message loop, top-level wiring; composes all
   the panels below. Owns `EngineClient`, `ColumnView`, `NavigationWorkspace`,
   `Renderer`, `SearchPanel`, `CommandPalette`, `FileOperations`, `Preview`,
   `StorageAnalysis`, `SettingsDialog`, `CategoryEngine`, `TreemapView`.
- `EngineClient` — UI-side client to the engine control pipe. Subscribes to
   generation notifications, requests directory enumeration/watching, maps and
   reads the shared-memory snapshot directly (`ReadSnapshot`), implements the
   "lazy start" (launches `FastFilesEngine.exe` directly if unreachable).
- `ColumnView` — Finder-style multi-column model + Direct2D/DirectWrite
   rendering; keyboard/mouse navigation; dual-pane; per-pane state save/restore.
- `NavigationWorkspace` — tabs, dual-pane, back/forward history, address bar
   (breadcrumb/editable), bookmarks, known-folder enumeration/re-resolution.
   Plain UI state (no engine handles) — all contexts share the one
   `EngineClient` snapshot.
- `NavigationChrome`, `NavigationSidebar`, `SearchPanel`, `CommandSystem`,
   `CommandPalette`, `QuickActions`, `Preview`, `Renderer`, `OleDragDrop`,
   `FileOperations`, `FileOperationPolicy`, `ConflictDialog`, `SelectionModel`,
   `StorageAnalysis`, `SettingsDialog`, `CategoryEngine`, `TreemapView`, `Util`
   — the rest of the shell surface (storage-analysis drill
   view consumes `RequestFolderAggregate` via `EngineClient`).
- `SettingsDialog` — settings/appearance configuration dialog.
- `CategoryEngine` — drives the category/treemap view mode.
- `TreemapView` — treemap visualization rendering.
- `Util` — shared UI utility functions.

## `FastFilesSetup` (installer) & `fftest` (probe)

- `src/installer/src/` — `Main.cpp`, `InstallSteps.cpp` (drives `ffsetup`),
  `ScratchPath.cpp`. Links `wer` (Windows Error Reporting).
- `src/fftest/src/Main.cpp` — privilege-diagnostics probe binary against the
  running service; links `ffindexsvcprobes`.

## Data flow (the two seams + the shared-memory shortcut)

1. **Engine↔Service (elevation boundary).** `VolumeSessionManager` issues
   scan/journal requests on `PrivilegedConnection`; the service streams
   `ScanRecordBatch`/`JournalRecordBatch` frames back on the same Ctrl pipe.
   `IndexPipeline` commits each batch to `Store` then applies to `Projection`.
   The scan/journal are implemented — dispatched to real
   `VolumeScanner`/`UsnJournalReader` worker threads — but inert in practice:
   placeholder signature pins (`PinnedSignatures.h`) make mutual auth fail
   closed, so the index is not populated from MFT/USN yet and degraded mode
   remains the active path.
2. **Engine→UI snapshot (zero IPC per read).** `IndexPipeline.ExportDirectorySnapshot`
   → `UiServer.MergeIndexDirectories` → `SnapshotPublisher.Publish` flips a
   generation in the mapped section. `EngineClient.ReadSnapshot` maps the
   section and parses it in-process; the control pipe only carries the
   "new generation ready" notification.
3. **Degraded path (active today).** `UiServer` calls `DegradedModeEnumerator`
   on `RequestDirectory`, `DirectoryWatcher` watches each browsed root, and
   each change re-enumerates + republishes a new generation — no service.

## Tests

Plain C++ executables registered with CTest (`Check(condition, description)`
helper, non-zero exit on failure — no gtest/catch). Test dirs mirror
components under `tests/`: `{protocol, ipc, indexstore, search, indexsvc,
engine, navigation, commands, fileoperations, preview, ui, uia-driver}`.
Files named `test_<behavior>.cpp`; benchmarks `bench_<behavior>.cpp`.

| Test target (`-R` matches this) | Source |
| --- | --- |
| `ffprotocol_tests`, `ffprotocol_diagnostics_tests`, `ffprotocol_fuzz_tests` | `tests/protocol/test_protocol.cpp`, `test_diagnostics.cpp`, `test_fuzz.cpp` |
| `ffipc_framing_tests` | `tests/ipc/test_pipe_framing.cpp` |
| `ffindexstore_store_tests`, `ffindexstore_projection_tests` | `tests/indexstore/test_store.cpp`, `test_projection.cpp` |
| `ffindexstore_bench_projection_memory` | `bench_projection_memory.cpp` (informational only, **not** `add_test`-registered) |
| `ffmftparser_tests`, `ffprivilege_diagnostics_tests`, `ffcommand_surface_tests`, `ffconnection_registry_tests` | `tests/indexsvc/test_mft_parser.cpp`, `test_privilege_diagnostics.cpp`, `test_command_surface.cpp`, `test_connection_registry.cpp` |
| `ffengine_index_pipeline_tests`, `ffengine_volume_session_manager_tests`, `ffengine_authenticode_verification_tests`, `ffengine_degraded_special_files_tests`, `ffengine_subtree_gating_tests` | `tests/engine/` |
| `ffsearch_tests`, `ffsearch_scan_tests` | `tests/search/test_query.cpp`, `test_search.cpp` |
| `ffnavigation_tests` | `tests/navigation/test_navigation_workspace.cpp` |
| `ffcommand_tests` | `tests/commands/test_command_system.cpp` |
| `fffileoperations_tests`, `fffileoperations_e2e_tests`, `ffselection_tests` | `tests/fileoperations/` |
| `ffpreview_tests` | `tests/preview/test_preview.cpp` |
| `fftreemap_layout_tests`, `ffui_style_tests` | `tests/ui/test_treemap_layout.cpp`, `test_ui_style.cpp` |
| `ffuia_driver_ps_tests`, `ffintake_gate_ps_tests` | PowerShell; registered in `tests/uia-driver/CMakeLists.txt` → `verify/uia-driver/tests/*.ps1` |

The two PowerShell tests are registered only when `find_program(pwsh)` finds
`pwsh`, so the total test count varies by environment.

Notable test pattern: `ffengine_*`, `fffileoperations_*`, `ffnavigation_tests`,
`ffcommand_tests`, and `ffpreview_tests` compile
specific `.cpp` files directly from `src/` (e.g. `IndexPipeline.cpp`,
`FileOperations.cpp`) rather than linking the executable target — so the
engine/UI stay testable without pulling in the whole GUI/service. Keep that
pattern when adding engine/UI unit tests. `test_fuzz.cpp` feeds malformed
frames/records at the parsers — update it when protocol parsing changes.

## Vendored & generated

- `third_party/sqlite/` — SQLite amalgamation, compiled as C for the `sqlite3`
  target only, keeps its own warning config; avoid incidental edits.
- Generated/untracked (do not commit): `build/`, `out/`, `.vs/`,
  `CMakeUserPresets.json`, `verify/runs/`, `verify/baselines/`.
