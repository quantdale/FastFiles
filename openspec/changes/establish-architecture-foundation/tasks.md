## 1. Project Scaffolding

- [ ] 1.1 Choose build system (CMake, targeting MSVC) and set up a multi-target C++ solution
- [ ] 1.2 Create repo structure for three executable targets — `FastFilesIndexSvc`, `FastFilesEngine`, `FastFiles` — plus a shared static library for the IPC protocol
- [ ] 1.3 Configure toolchain for x64 Windows 10 1809+/Windows 11, with warnings-as-errors and standard hardening compiler/linker flags
- [ ] 1.4 Verify all three targets build cleanly from a fresh checkout

## 2. Shared IPC Protocol Library

- [ ] 2.1 Define the 8-byte frame header (`TotalLength`, `StructVersion`, `MessageType`) with encode/decode helpers
- [ ] 2.2 Implement a protocol-wide maximum frame size constant, enforced (in `size_t`/`u64` arithmetic, never the `u32` field width) before any buffer allocation
- [ ] 2.3 Define the closed engine→service command enum and payload structs: `Handshake`, `EnumerateVolumes`, `StartVolumeScan(VolumeId)`, `OpenUsnJournal(VolumeId, ResumeUsn)`, `StopVolumeScan(VolumeId)`, `CloseUsnJournal(VolumeId)`, `Heartbeat`
- [ ] 2.4 Define `MftRecordV1`/`UsnDeltaV1` record struct layouts (fields only — parsing logic is deferred) with explicit length-prefixed field validation helpers (reject out-of-range length, never silently clamp)
- [ ] 2.5 Implement bounds-checked message dispatch (switch-with-default or map lookup) that rejects and logs unrecognized `MessageType`/`StructVersion` values — no raw jump table indexed by an untrusted integer
- [ ] 2.6 Implement wire-protocol version negotiation (`{Major, Minor}`) and the explicit `IncompatibleVersion` reply
- [ ] 2.7 Unit tests: oversized frame rejected before allocation; record-count/payload-size mismatch rejected before parsing; out-of-range length-prefixed field rejects the whole record; unrecognized `MessageType` rejected without crashing

## 3. FastFilesIndexSvc (privileged service)

- [ ] 3.1 Implement SCM service registration: create the virtual service account, grant `SeBackupPrivilege` only via `LsaAddAccountRights`
- [ ] 3.2 Create the Ctrl and Data named pipes with an explicit DACL (authorized client group + `SYSTEM` only) and `FILE_FLAG_FIRST_PIPE_INSTANCE`; fail loudly and log on naming collision
- [ ] 3.3 Harden binary loading: `SetDefaultDllDirectories` before any `LoadLibrary`, fully-qualified paths or static linking for every dependency
- [ ] 3.4 Implement `Handshake` with mutual authentication: verify the connecting client's image path (under the ACL-locked install directory) and pinned Authenticode signature thumbprint, in addition to checking client group membership
- [ ] 3.5 Implement periodic re-validation of connected clients' identity/membership for long-lived connections (not only at initial `Handshake`)
- [ ] 3.6 Implement `EnumerateVolumes` returning only fixed local NTFS/ReFS volumes discovered by the service itself
- [ ] 3.7 Implement opaque, connection-scoped `VolumeId`/`JournalId` assignment; reject `Stop`/`Close` requests from a non-owning connection; auto-teardown open scans/journals on disconnect
- [ ] 3.8 Implement `StartVolumeScan`/`OpenUsnJournal` as explicit, well-defined "not yet implemented" responses (real MFT/USN parsing lands in a follow-up change)
- [ ] 3.9 Implement self-directed staleness detection (compare on-disk binary hash vs. loaded binary on a timer and opportunistically at handshake) with abnormal self-termination on mismatch
- [ ] 3.10 Configure SCM failure actions (restart delay, reset window, capped action count) as part of service registration, covering both crash recovery and the self-heal path from 3.9
- [ ] 3.11 Set the service's SCM security descriptor so the client group has only `SERVICE_QUERY_STATUS`/`SERVICE_QUERY_CONFIG` — never `SERVICE_START`/`STOP`/`CHANGE_CONFIG`/`WRITE_DAC`/`WRITE_OWNER`
- [ ] 3.12 Lock service log/crash-dump paths to admin-only read/write; disable or redirect WER local dumps away from any user-readable default location

## 4. FastFilesEngine (unprivileged index owner)

- [ ] 4.1 Register the per-user Scheduled Task (logon trigger, `RunLevel=LeastPrivilege`); support lazy start from the first UI instance if the task hasn't fired yet
- [ ] 4.2 Implement the privileged-connection state machine (`Disconnected → Connecting → Handshaking → Active`) with exponential backoff reconnect (capped)
- [ ] 4.3 Implement heartbeat send/receive with a timeout-based force-close so a deadlocked-but-not-broken pipe still transitions to `Disconnected`
- [ ] 4.4 Implement the engine-side service verification (image path + pinned Authenticode signature) before trusting any connected `FastFilesIndexSvc`
- [ ] 4.5 Implement degraded-mode directory enumeration via `FindFirstFileEx`, skipping ACL-inaccessible subfolders without failing the whole listing
- [ ] 4.6 Implement `ReadDirectoryChangesW` watches for browsed/pinned directories while in degraded mode
- [ ] 4.7 Implement the read-only, double-buffered memory-mapped snapshot publication mechanism plus control-pipe "new generation" notifications to subscribed UI clients
- [ ] 4.8 Implement per-session pipe/mapping naming (suffixed by session ID) to support multiple concurrently logged-on sessions
- [ ] 4.9 Implement version-aware reconnection: treat a product build-version mismatch or wire-protocol major-version mismatch as "privileged path unavailable," drop to degraded mode, surface an actionable status
- [ ] 4.10 Implement idle-based privileged-connection drop (configurable idle period with no UI window open and no significant recent change volume) with re-establishment on next UI launch or activity burst

## 5. FastFiles (UI shell) — Column View Browsing

- [ ] 5.1 Set up the Direct2D/DirectComposition window shell: device-independent resources, resize handling, basic message loop
- [ ] 5.2 Implement the Engine↔UI control-pipe client connection: subscribe to snapshot-generation notifications, request initial directory listings
- [ ] 5.3 Implement multi-column layout: one column per hierarchy level, populating a new column to the right on folder selection while preserving prior columns
- [ ] 5.4 Implement column replacement when a different folder is selected within an already-expanded column
- [ ] 5.5 Implement visual distinction between files and folders; only folders populate a new column
- [ ] 5.6 Implement visual distinction for the selected item within a column
- [ ] 5.7 Implement horizontal scrolling when total column width exceeds the viewport
- [ ] 5.8 Implement in-column error states: permission-denied message, "no longer available" message for a directory that disappeared between listing and selection
- [ ] 5.9 Implement the non-modal engine-connection-state status badge (e.g., "Instant search: basic — click to enable")
- [ ] 5.10 Implement basic keyboard navigation within and across columns (arrow keys, Enter to descend)

## 6. Installer

- [ ] 6.1 Build the installer: create the authorized client local group and add the installing user, register `FastFilesIndexSvc` under its virtual account, ACL the install directory (Admin/TrustedInstaller write-only) and both named pipes, register the `FastFilesEngine` scheduled task
- [ ] 6.2 Harden installer custom actions against TOCTOU: randomized per-run scratch paths, reject reparse points when creating installer scratch files
- [ ] 6.3 Reapply install-directory and pipe ACLs on every upgrade, not only first install
- [ ] 6.4 Implement the uninstall path, reversing everything from 6.1

## 7. Security & Integration Validation

- [ ] 7.1 Empirically verify `SeBackupPrivilege` alone (non-admin token) is sufficient for the raw volume handle open and `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL` calls, since this underpins the privilege-minimization design even though real scan/journal logic is stubbed in this change
- [ ] 7.2 Adversarially test the pipe-squatting race: confirm `FILE_FLAG_FIRST_PIPE_INSTANCE` causes a hard, logged failure when a pipe of the expected name already exists
- [ ] 7.3 Test that a process which is not the genuine `FastFilesEngine.exe` (wrong image path or signature) cannot progress past `Handshake` rejection when connecting directly to the privileged pipes
- [ ] 7.4 Test that the authorized client group cannot start or stop `FastFilesIndexSvc` via SCM APIs
- [ ] 7.5 Test end-to-end Column View browsing with `FastFilesIndexSvc` not installed, stopped, and killed mid-session — confirm the UI never hangs or crashes and always reaches degraded mode with a clear status
- [ ] 7.6 Load-test concurrent multi-session connections against a single `FastFilesIndexSvc` instance
- [ ] 7.7 Fuzz the frame parser (oversized frames, record-count mismatches, out-of-range length-prefixed fields) against both the service and the engine
