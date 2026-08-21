# Audit area: INSTALL/SETUP LIFECYCLE — audit src/installer/, src/setup/ (ffsetup library: service registration, group creation, ACLs, scheduled-task registration, Authenticode verification seams), build_and_deploy.ps1, VERSION and cmake/version metadata. STATIC ANALYSIS ONLY — do not execute installers, do not touch system services, groups, tasks, or certificate stores. Cover: clean-install step completeness and ordering, upgrade-over-previous-version and repair paths, uninstall completeness and user-data policy, interrupted/failed install leaving half-registered state, duplicate engine/service instance prevention, ACL scoping correctness, version metadata consistency, clear error reporting on failure.

(agent agent-10, outcome completed)

## Defects

### [P1] Upgrade always fails while the app is in use — installer never stops the UI or engine processes — src/installer/src/InstallSteps.cpp:760
- Symptom (user-visible): Running the new installer over an existing installation while FastFiles.exe/FastFilesEngine.exe are running (engine auto-starts at logon via the scheduled task) aborts with "failed to stage/install binaries" and rolls back; the product can effectively never be upgraded on a logged-in machine.
- Evidence: `InstallSteps.cpp:760-764` stages all four binaries unconditionally; `PackageTrust.cpp:1119` promotes via `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` with the comment "promotion must remain same-volume rename". Only the *service* is stopped (`StopIndexService()`, InstallSteps.cpp:716); there is no `OpenProcess`/graceful-shutdown of `FastFilesEngine.exe` or `FastFiles.exe` anywhere in `src/installer/` (grep confirms zero hits).
- Failure scenario: User logs in → scheduled task starts `FastFilesEngine.exe` → user runs the new installer → `StageAndInstallBinary(..., kEngineExeName, ...)` hits a sharing violation replacing the running image → whole upgrade rolls back.
- Root cause hypothesis: quiescence step was designed only around the SCM-visible service; per-user processes were forgotten.
- Suggested fix: before staging, gracefully signal/terminate running `FastFilesEngine.exe` instances (per-session control pipe already exists: `kUiCtrlPipeNameFormat`) and prompt/close `FastFiles.exe`; fail with a clear "close FastFiles and retry" message rather than a raw rollback.
- Regression test idea: seam-injected test asserting the install plan includes a "client processes stopped" step before any `PromoteVerifiedPackageFile`, and that a locked destination produces a targeted error, not a generic rollback.

### [P1] Service stop is never awaited before DeleteService → intermittent `ERROR_SERVICE_MARKED_FOR_DELETE` on upgrade re-register — src/setup/src/ServiceRegistration.cpp:318
- Symptom: upgrades intermittently fail with "failed to register FastFilesIndexSvc" (or during rollback, "failed to re-register the prior service"), leaving the machine with **no** service registration at all.
- Evidence: `UnregisterIndexService()` issues `ControlService(service, SERVICE_CONTROL_STOP, &status);` (line 319) and immediately calls `DeleteService(service)` (line 321) with no wait-for-`SERVICE_STOPPED` loop (contrast `StopIndexService()` in InstallSteps.cpp:295-305, which does poll). The upgrade path then calls `RegisterIndexService` milliseconds later (`InstallSteps.cpp:775-781`). SCM completes the deletion only after the process exits and all handles close; `CreateServiceW` on a pending-delete name returns `ERROR_SERVICE_MARKED_FOR_DELETE`.
- Failure scenario: slow service shutdown (flushing state) → `DeleteService` marks pending → `CreateServiceW` fails → `rollback()` → `RestoreServiceState` calls `UnregisterIndexService` again (same race) → machine left service-less despite "rollback succeeded" being reported only if the second register happens to win.
- Root cause: missing wait-for-stopped between stop and delete.
- Suggested fix: in `UnregisterIndexService`, poll `QueryServiceStatusEx` until `SERVICE_STOPPED` (bounded, e.g. 30 s) before `DeleteService`, and treat delete-pending as retryable in `RegisterIndexService`.
- Regression test idea: fake-SCM/seam test where stop completion is delayed past `DeleteService`; assert register retries or waits instead of failing.

### [P1] Self-upgrade from the installed `FastFilesSetup.exe` is impossible — installer tries to replace its own running image — src/installer/src/InstallSteps.cpp:763
- Symptom: double-clicking the *installed* `FastFilesSetup.exe` (the natural "check for updates / repair" affordance, and the binary the uninstall registry entry points at) to upgrade always fails at "failed to stage/install binaries".
- Evidence: `PackageBinaryNames()` includes `kInstallerExeName` (line 108) and it is staged last (line 763); promotion is `MoveFileExW(REPLACE_EXISTING)` onto `installDir\FastFilesSetup.exe`, which is the executing image → `ERROR_SHARING_VIOLATION`. There is no special case (no rename-aside, no `MOVEFILE_DELAY_UNTIL_REBOOT`, no detection "source == destination").
- Failure scenario: user runs `C:\Program Files\FastFiles\FastFilesSetup.exe` directly → stage of `FastFilesEngine.exe` succeeds, stage of `FastFilesSetup.exe` fails → full rollback → exit 1.
- Root cause: no self-replacement strategy.
- Suggested fix: detect `_wcsicmp(sourcePath, installFilePath)==0` and skip re-staging that file (it's already the verified payload), or rename the running image aside before promotion.
- Regression test idea: unit test on the staging plan for source-dir == install-dir.

### [P2] Failed-upgrade rollback leaves new-version uninstall metadata and scheduled task behind — src/installer/src/InstallSteps.cpp:801
- Symptom: after a rolled-back upgrade, Add/Remove Programs shows the **new** version number while the restored binaries are the old version; the spec promise "Failed upgrade restores the prior service" is only partially honored.
- Evidence: `RegisterUninstallMetadata(installerPath, installDir)` (line 801) writes `DisplayVersion = ffsetup::kProductVersion` (new build) *before* `VerifyServiceStartedFresh()` (line 809) can fail; `RestoreServiceState` (lines 497-543) restores binaries, manifest, and service — but never rewrites or removes the uninstall key, and the fresh-install rollback lambda (743-758) isn't used for upgrades.
- Failure scenario: upgrade v0.2.0→v0.3.0 fails startup verification → rollback restores v0.2.0 payload → registry still says 0.3.0.
- Suggested fix: in `RestoreServiceState`, rewrite uninstall metadata from the prior manifest's version (or delete it if the prior install had none).
- Regression test idea: hosted test asserting registry version equals prior-manifest version after a forced verification failure.

### [P2] Uninstall reports failure and leaves binaries because running UI/engine processes are never terminated — src/installer/src/Main.cpp:157
- Symptom: uninstalling while the app is open exits 1 ("not clean"), leaves `FastFilesEngine.exe`/`FastFiles.exe` on disk, and both processes keep running with their scheduled task/service gone.
- Evidence: `RunUninstall` (InstallSteps.cpp:818-876) removes task/service/group/registry, then deletes every child file; `DeleteFileW` on a running image fails and sets `clean=false` (lines 859-861), and there is no process-termination step anywhere in the uninstall path.
- Failure scenario: engine started at logon → uninstall → engine exe locked → exit 1, orphaned engine keeps holding the (now-deleted) snapshot/watchers.
- Suggested fix: signal/terminate engine and UI processes before deleting files (same mechanism as the P1 fix above).
- Regression test idea: plan-level test that uninstall order is task→processes→service→files.

### [P2] No single-instance guard on the installer — concurrent installs race on SCM and the Backup directory — src/installer/src/InstallSteps.cpp:655
- Symptom: two simultaneous elevated installs (double-click twice; UAC prompts are easy to approve twice) interleave `UnregisterIndexService`/`RegisterIndexService`, producing nondeterministic half-states; one run can delete the other's just-created service.
- Evidence: `RunInstall` has no named-mutex acquisition; the only serialization is accidental (`BackupServiceState`'s `CreateDirectoryW(backupDir)` fails if it exists, line 411 — which aborts one run, but only *after* the other has already stopped the service and possibly mid-way through staging).
- Suggested fix: create a named mutex (e.g. `Local\FastFiles.Install`) at the top of `wmain` install/uninstall paths; exit with a clear "another install is in progress" if it exists.
- Regression test idea: logic test on the guard helper; manual dual-run smoke.

### [P3] Fresh-install failure leaves the security group, user membership, and an empty install dir behind — src/installer/src/InstallSteps.cpp:732
- Symptom: an aborted first install leaves `FastFilesUsers` created and the installing user added to it, plus an empty `C:\Program Files\FastFiles` directory — surprising residue for a "failed" install.
- Evidence: `CreateAuthorizedClientGroupAndAddUser(userName)` runs at line 732, before staging; the fresh-install `rollback` lambda (743-758) undoes service/task/metadata/ACL/binaries but never calls `ffsetup::DeleteAuthorizedClientGroup()` or `RemoveDirectoryW(installDir)`.
- Suggested fix: track whether the group was newly created this run and remove it (and the empty install dir) in the fresh-install rollback.
- Regression test idea: extend the fresh-failure rollback coverage to assert group absence.

### [P3] Startup verification silently passes when the startup-token diagnostic line is absent — src/installer/src/InstallSteps.cpp:618
- Symptom: the gate meant to prove the fresh token holds `SeBackupPrivilege` degrades to "service stayed up for 2.5 s" whenever the diagnostic log is unavailable or the line wasn't written — a degraded check with only a stderr note.
- Evidence: `"startup-token diagnostic not found -- proceeding (log unavailable)"` then `return true` (lines 615-620). The comment acknowledges tolerance, but combined with the service's own startup continuing regardless, a privilege-broken install can pass verification and delete the rollback backup (line 814).
- Suggested fix: distinguish "log file unreadable" (proceed) from "log readable but no `state=startup-token` line for this start" (fail), using the recorded byte offset.
- Regression test idea: inject a readable log lacking the token line; expect verification failure.

## Verified sound
- Version metadata chain: `VERSION` → `cmake/FastFilesVersion.cmake` validation (3 numeric components ≤65535, 40-hex git build ID, fail-closed) is consistent; `CompareProductVersions`/`ParseProductVersion` correctly reject leading zeros, >3 components, overflow.
- Downgrade/same-version protection on upgrade (`InstallSteps.cpp:708-715`): rejects downgrades and ambiguous same-version packages before touching anything.
- Upgrade backup integrity: prior payload must itself pass trust validation before backup; backup manifest is written canonically, re-read, and re-verified; pre-existing `Backup` dir aborts rather than becoming a trusted rollback source (`BackupServiceState`, lines 403-458).
- Scratch-directory hardening (`ScratchPath.cpp`): GUID-named under ProgramData, reparse-point checks at each level, CREATE_NEW-only; promotion is same-volume rename with post-promotion re-verification; copy re-hashes the source handle afterward to detect TOCTOU swaps.
- Service DACL scoping (`SecurityDescriptors.cpp:106-122`): client group gets only `SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG` — no start/stop/config, matching the documented invariant; install-dir DACL is read/execute for group and service SID, write for SYSTEM/Admins only, applied protected/non-inheriting.
- Service binary path is unconditionally quoted against `C:\Program.exe` splitting (`ServiceRegistration.cpp:205`); LocalSystem privilege grant correctly skipped as inherent no-op.
- Registration transactionality inside `RegisterIndexService`: post-create failures delete the service and revoke the privilege grant.
- Installer elevation flow: signature/manifest verified before UAC relaunch; uninstall stays recoverable via signature-only check when the manifest is lost; POSIX-semantics self-delete with reboot-move fallback (no helper in a writable dir).
- Scheduled-task registration: correct COM scope handling (incl. `RPC_E_CHANGED_MODE`), TASK_CREATE_OR_UPDATE, group-logon LUA principal, no execution time limit, `TASK_INSTANCES_IGNORE_NEW`; unregister is idempotent for missing folder/task.
- Uninstall refuses to operate through a reparse-point install root and never follows child junctions; WER LocalDumps key and ProgramData tree are cleaned.
