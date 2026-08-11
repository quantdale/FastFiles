# Elevated release-validation evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809` at `feef52d0cf4bc05ff20e0cf63eaaf1fcdbb77a73`.

This evidence records what elevation unlocked. It does not provision release
signing material, replace placeholder pins, or claim an authenticated
privileged path that was not exercised.

## Environment

- User: `LAPTOP-8T16MFM8\Michael Roy`.
- Administrator role: true; `whoami /groups` showed `BUILTIN\Administrators`
  enabled and `High Mandatory Level`.
- Architecture: x64 Windows 11 Home Single Language, build 26200.
- The installed service was present before validation and was running as
  `LocalSystem`; the repository's Visual Studio 2022 Build Tools developer
  shell was used for the build gates.

## Gate matrix

| Gate | Result | Evidence |
| --- | --- | --- |
| 4.2 negative authentication | Partial; remains open | Existing unsigned/mismatch/placeholder/partial-pin tests pass. A disposable untrusted signer was rejected by the repository's native WinVerifyTrust helper. Windows refused to sign the disposable expired certificate, leaving that fixture `NotSigned`; no expired-signed result is claimed. |
| 4.5 signed install and active/degraded transition | BLOCKED — authorized signing material required | The installed service, engine, and setup binaries are `NotSigned`; source pins remain all-zero placeholders. |
| 4.6 authenticated MFT/USN path | BLOCKED — authorized signing material required | Independent NTFS/raw-volume/USN checks pass; the signed engine↔service path was not activated. |
| 4.7 `fftest` against the running service | Partial; task remains open | Elevated token/raw-volume/USN and impostor-rejection probes pass. Authenticated running-service coverage is blocked by unsigned peers/placeholders. |
| 4.8 clean-host matrix | BLOCKED — clean host required | This is the development host, not a fresh Windows image or clean supported environment. |
| 5.1–5.3 interactive visual matrix | BLOCKED — interactive desktop evidence required | The computer-use package lacked its required `documentation()` API, so no undocumented desktop automation or fabricated screenshot evidence was used. |

## Service and installer

- Stop/start lifecycle: PASS. The service transitioned `Running → Stopped →
  Running` and returned to its original running state.
- Idempotent staging upgrade: PASS, exit 0; staged binaries were byte-for-byte
  identical to the installed binaries.
- Uninstall: PASS, exit 0; SCM service, install directory, scheduled task,
  authorized group, uninstall metadata, and service pipe were absent after
  cleanup.
- Reinstall: PASS, exit 0; service, task, group, metadata, pipe, and binaries
  were restored. The final service is `LocalSystem`, automatic delayed start,
  and running.
- SCM descriptor: PASS. The installed service descriptor gives the client
  group query-only `CCLC` rights and does not grant start/stop/control rights.
- Install-directory ACL and descriptor-builder pipe assertions: PASS in the
  elevated probe and `ffsetup_security_descriptor_tests`. The direct generic
  file-security helper does not accept a named-pipe path (`ERROR_INVALID_PARAMETER`),
  so that probe result is not treated as a product failure.
- Scratch directory was empty after reinstall; test-only TEMP databases and
  certificate fixtures were removed.

## Privilege and low-level probes

- `fftest token`: PASS; elevated token, integrity RID 12288.
- `fftest privilege`: PASS; `\\.\C:` opened and the USN journal was queried.
- Direct candidate-matrix runs under the Administrator control context passed
  raw-volume open, USN query, and USN read. They do not substitute for the
  documented non-admin narrow-candidate rows or the LocalSystem service row.
- `fftest handshake-impostor`: PASS; the impostor was rejected with
  `UnverifiedImagePath`.
- `C:` is NTFS, read/write, supports USN, and `fsutil usn queryjournal C:`
  returned a live journal. The MFT valid-data length was reported by
  `fsutil fsinfo ntfsinfo C:`. No raw MFT enumeration claim is made outside
  the authenticated production path; parser/fuzz tests remain PASS.

## Authentication state

- `src/setup/include/ffsetup/PinnedSignatures.h` still contains both
  all-zero placeholders.
- Installed `FastFilesIndexSvc.exe`, `FastFilesEngine.exe`, and
  `FastFilesSetup.exe` are unsigned. A Debug service fixture signed by the
  pre-existing test certificate verifies locally, but it is not production
  trust and was not placed in the configured pins.
- No pin, Authenticode check, UAC/security control, or diagnostic escape hatch
  was weakened. No private signing material was added to the repository.
- Authorized production signing material: unavailable.

## Repository gates

- Pinned OpenSpec CLI: 45 passed, 0 failed.
- Debug build: PASS.
- Analyze build: PASS.
- Serial CTest: 43/43 PASS, including `ffprotocol_fuzz_tests` and
  `ffdoc_drift_tests`. One earlier run timed out only
  `ffengine_volume_session_manager_tests`; its focused rerun passed in 0.17 s
  and the subsequent complete run passed in 28.44 s.

## Release decision

The code/build/test gates are healthy, but signed authenticated service
activation, expired-signer release evidence, clean-host coverage, and the
interactive visual matrix remain external blockers. The correct state is:

`CODE COMPLETE  RELEASE VALIDATION STILL BLOCKED`

## Addendum — authorized visual pass

The earlier 5.1–5.3 row above predates the user's explicit authorization for
temporary display changes. The subsequent Computer Use pass is recorded in
`evidence/interactive-visual-validation-20260811.md`: task 5.2 (dark-theme
confirmation) and task 5.3 (retained screenshot evidence) are complete. Task
5.1 remains open only for the real aggregate-backed `% of Parent` sum; the
degraded drill-down exposed the column but returned unknown/partial parent
totals, so no 100%-sum claim is made.
