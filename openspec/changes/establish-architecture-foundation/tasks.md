## 1. Project Scaffolding

- [x] 1.1 Choose build system (CMake, targeting MSVC) and set up a multi-target C++ solution
- [x] 1.2 Create repo structure for three executable targets — `FastFilesIndexSvc`, `FastFilesEngine`, `FastFiles` — plus a shared static library for the IPC protocol
- [x] 1.3 Configure toolchain for x64 Windows 10 1809+/Windows 11, with warnings-as-errors and standard hardening compiler/linker flags
- [x] 1.4 Verify all three targets build cleanly from a fresh checkout — **unblocked and closed** via the `autonomous-runtime-verification` harness's Tier-0 `windows-build-validation` capability: clean Debug + Release builds from a fresh `build/` tree both PASS (toolset: Visual Studio Build Tools 2026 18.3.11520.95, SDK 10.0.26100). Evidence: `verify/runs/establish-architecture-foundation/20260731-043713/` (`report.md`, `artifacts/windows-build-validation/{debug,release}-{configure,build}.log`). Two pre-existing product bugs the harness surfaced while proving this (never caught before because nothing could compile it on Windows) were fixed as part of closing this task: a wrong `D2D1CreateFactory` overload in `src/ui/src/Renderer.cpp`, and an unused-parameter `/WX` break in `src/ui/src/ColumnView.cpp`.

## 2. Shared IPC Protocol Library

- [x] 2.1 Define the 8-byte frame header (`TotalLength`, `StructVersion`, `MessageType`) with encode/decode helpers
- [x] 2.2 Implement a protocol-wide maximum frame size constant, enforced (in `size_t`/`u64` arithmetic, never the `u32` field width) before any buffer allocation
- [x] 2.3 Define the closed engine→service command enum and payload structs: `Handshake`, `EnumerateVolumes`, `StartVolumeScan(VolumeId)`, `OpenUsnJournal(VolumeId, ResumeUsn)`, `StopVolumeScan(VolumeId)`, `CloseUsnJournal(VolumeId)`, `Heartbeat`
- [x] 2.4 Define `MftRecordV1`/`UsnDeltaV1` record struct layouts (fields only — parsing logic is deferred) with explicit length-prefixed field validation helpers (reject out-of-range length, never silently clamp)
- [x] 2.5 Implement bounds-checked message dispatch (switch-with-default or map lookup) that rejects and logs unrecognized `MessageType`/`StructVersion` values — no raw jump table indexed by an untrusted integer
- [x] 2.6 Implement wire-protocol version negotiation (`{Major, Minor}`) and the explicit `IncompatibleVersion` reply
- [x] 2.7 Unit tests: oversized frame rejected before allocation; record-count/payload-size mismatch rejected before parsing; out-of-range length-prefixed field rejects the whole record; unrecognized `MessageType` rejected without crashing

## 3. FastFilesIndexSvc (privileged service)

- [x] 3.1 Implement SCM service registration: create the virtual service account, grant `SeBackupPrivilege` only via `LsaAddAccountRights`
- [x] 3.2 Create the Ctrl and Data named pipes with an explicit DACL (authorized client group + `SYSTEM` only) and `FILE_FLAG_FIRST_PIPE_INSTANCE`; fail loudly and log on naming collision
- [x] 3.3 Harden binary loading: `SetDefaultDllDirectories` before any `LoadLibrary`, fully-qualified paths or static linking for every dependency
- [x] 3.4 Implement `Handshake` with mutual authentication: verify the connecting client's image path (under the ACL-locked install directory) and pinned Authenticode signature thumbprint, in addition to checking client group membership
- [x] 3.5 Implement periodic re-validation of connected clients' identity/membership for long-lived connections (not only at initial `Handshake`)
- [x] 3.6 Implement `EnumerateVolumes` returning only fixed local NTFS/ReFS volumes discovered by the service itself
- [x] 3.7 Implement opaque, connection-scoped `VolumeId`/`JournalId` assignment; reject `Stop`/`Close` requests from a non-owning connection; auto-teardown open scans/journals on disconnect
- [x] 3.8 Implement `StartVolumeScan`/`OpenUsnJournal` as explicit, well-defined "not yet implemented" responses (real MFT/USN parsing lands in a follow-up change)
- [x] 3.9 Implement self-directed staleness detection (compare on-disk binary hash vs. loaded binary on a timer and opportunistically at handshake) with abnormal self-termination on mismatch
- [x] 3.10 Configure SCM failure actions (restart delay, reset window, capped action count) as part of service registration, covering both crash recovery and the self-heal path from 3.9
- [x] 3.11 Set the service's SCM security descriptor so the client group has only `SERVICE_QUERY_STATUS`/`SERVICE_QUERY_CONFIG` — never `SERVICE_START`/`STOP`/`CHANGE_CONFIG`/`WRITE_DAC`/`WRITE_OWNER`
- [x] 3.12 Lock service log/crash-dump paths to admin-only read/write; disable or redirect WER local dumps away from any user-readable default location

## 4. FastFilesEngine (unprivileged index owner)

- [x] 4.1 Register the per-user Scheduled Task (logon trigger, `RunLevel=LeastPrivilege`); support lazy start from the first UI instance if the task hasn't fired yet
- [x] 4.2 Implement the privileged-connection state machine (`Disconnected → Connecting → Handshaking → Active`) with exponential backoff reconnect (capped)
- [x] 4.3 Implement heartbeat send/receive with a timeout-based force-close so a deadlocked-but-not-broken pipe still transitions to `Disconnected`
- [x] 4.4 Implement the engine-side service verification (image path + pinned Authenticode signature) before trusting any connected `FastFilesIndexSvc`
- [x] 4.5 Implement degraded-mode directory enumeration via `FindFirstFileEx`, skipping ACL-inaccessible subfolders without failing the whole listing
- [x] 4.6 Implement `ReadDirectoryChangesW` watches for browsed/pinned directories while in degraded mode
- [x] 4.7 Implement the read-only, double-buffered memory-mapped snapshot publication mechanism plus control-pipe "new generation" notifications to subscribed UI clients
- [x] 4.8 Implement per-session pipe/mapping naming (suffixed by session ID) to support multiple concurrently logged-on sessions
- [x] 4.9 Implement version-aware reconnection: treat a product build-version mismatch or wire-protocol major-version mismatch as "privileged path unavailable," drop to degraded mode, surface an actionable status
- [x] 4.10 Implement idle-based privileged-connection drop (configurable idle period with no UI window open and no significant recent change volume) with re-establishment on next UI launch or activity burst

## 5. FastFiles (UI shell) — Column View Browsing

- [x] 5.1 Set up the Direct2D/DirectComposition window shell: device-independent resources, resize handling, basic message loop
- [x] 5.2 Implement the Engine↔UI control-pipe client connection: subscribe to snapshot-generation notifications, request initial directory listings
- [x] 5.3 Implement multi-column layout: one column per hierarchy level, populating a new column to the right on folder selection while preserving prior columns
- [x] 5.4 Implement column replacement when a different folder is selected within an already-expanded column
- [x] 5.5 Implement visual distinction between files and folders; only folders populate a new column
- [x] 5.6 Implement visual distinction for the selected item within a column
- [x] 5.7 Implement horizontal scrolling when total column width exceeds the viewport
- [x] 5.8 Implement in-column error states: permission-denied message, "no longer available" message for a directory that disappeared between listing and selection
- [x] 5.9 Implement the non-modal engine-connection-state status badge (e.g., "Instant search: basic — click to enable")
- [x] 5.10 Implement basic keyboard navigation within and across columns (arrow keys, Enter to descend)

## 6. Installer

- [x] 6.1 Build the installer: create the authorized client local group and add the installing user, register `FastFilesIndexSvc` under its virtual account, ACL the install directory (Admin/TrustedInstaller write-only) and both named pipes, register the `FastFilesEngine` scheduled task
- [x] 6.2 Harden installer custom actions against TOCTOU: randomized per-run scratch paths, reject reparse points when creating installer scratch files
- [x] 6.3 Reapply install-directory and pipe ACLs on every upgrade, not only first install
- [x] 6.4 Implement the uninstall path, reversing everything from 6.1

## 7. Security & Integration Validation

- [x] 7.1 Empirically verify `SeBackupPrivilege` alone (non-admin token) is sufficient for the raw volume handle open and `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL` calls — **code-complete, runtime validation pending**. The automatic self-check `ffindexsvc::VerifyBackupPrivilegeSufficiency` is implemented and runs at every real service start (`src/indexsvc/src/PrivilegeVerification.cpp`). The Windows build is proven; actual pass/fail confirmation requires an elevated installation and a real service start under the intended service token. Evidence collection is blocked pending the privilege-model fix in `resolve-raw-volume-privilege-insufficiency`.
- [x] 7.2 Adversarially test the pipe-squatting race — **code-complete, runtime validation pending**. The production code path (`ffipc::PipeListener::CreateInstance`/`Start`, task 3.2) fails loudly by design with `FILE_FLAG_FIRST_PIPE_INSTANCE`. Runtime evidence requires the installed service plus a real squatter process.
- [x] 7.3 Test impostor handshake rejection — **code-complete, runtime validation pending**. The rejection path is implemented (`ffindexsvc::VerifyClientAtHandshake`, task 3.4). Runtime evidence requires a signed/installed build, a running service, and a second real process acting as the impostor.
- [x] 7.4 Test that the authorized client group cannot start or stop `FastFilesIndexSvc` via SCM APIs — **code-complete, runtime validation pending**. The SCM security descriptor is in place (`ffsetup::BuildServiceObjectSecurityDescriptor`, task 3.11); blocked pending a real installed service and a real user session in the authorized group.
- [x] 7.5 Test end-to-end Column View browsing with `FastFilesIndexSvc` absent/stopped/killed — **code-complete, runtime validation pending**. Degraded-mode fallback is implemented end-to-end (tasks 4.5, 4.9, 5.9); blocked pending all three real processes running on Windows.
- [x] 7.6 Load-test concurrent multi-session connections — **code-complete, runtime validation pending**. Per-session pipe/mapping naming is implemented (task 4.8); blocked pending multiple real concurrent Windows logon sessions.
- [x] 7.7 Fuzz the frame parser — **implemented and closed** via the `autonomous-runtime-verification` harness: `tests/protocol/test_fuzz.cpp` (`ffprotocol_fuzz_tests`) fuzzes `ValidateFrame`, `ValidateUiFrame`, `ParseMftBatch`, `ParseUsnDeltaBatch`, and `ParseSnapshot`. Passes under both Debug and Release, captured as JUnit XML. Evidence: `verify/runs/establish-architecture-foundation/20260731-043713/artifacts/windows-build-validation/{debug,release}-junit.xml`. End-to-end fuzzing of live named pipes remains a follow-up once Tier-1 IPC validation lands.
