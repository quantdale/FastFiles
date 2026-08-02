# Candidate privilege matrix definition

> Owner change: `resolve-raw-volume-privilege-insufficiency`
> Task: 1.1 — define the supported Windows hosts, NTFS volumes, and documented
> account/right candidates to evaluate, including a LocalSystem or
> Administrators control that is explicitly marked diagnostic-only.
> Status: definition complete. Execution is task 1.3; selection is task 1.4.

## Why this matrix exists

The completed `diagnose-backup-privilege-sufficiency` change captured, at the
real `VolumeScanner` `CreateFileW` call site under the installed
`NT SERVICE\FastFilesIndexSvc` token, that `SeBackupPrivilege` was **held and
enabled** yet `CreateFileW("\\\\.\\C:")` still returned `ERROR_ACCESS_DENIED`
(`0x5`) (see `evidence/real-service-outcome.md` in that change). That rules out
a missing/disabled-token explanation and establishes a genuine privilege-model
gap: `SeBackupPrivilege` alone is insufficient for the raw-volume **device**
open that scanning requires.

This document defines the fixed, reproducible candidate matrix that task 1.3
runs on a clean disposable host to identify the narrowest privilege or
service-identity change that lets the installed service open the raw volume and
perform the USN journal operations — or, if none does, to produce the evidence
that a constrained privileged broker is required.

## Selection rule (from design.md)

The deployed model is the **narrowest** candidate that passes all required
raw-volume and USN operations and preserves the existing signed-image /
authorization-group IPC boundary. Administrators-group membership and
LocalSystem are **controls or last-resort options only**; adopting either as
production requires an explicit security decision and a follow-up architecture
record (see spec scenario "Broad control does not silently become production
configuration").

## Supported Windows hosts

A clean, disposable host is required for every row so a candidate's apparent
pass cannot be explained by unrelated administrative state on a developer
machine (design.md risk mitigation). Each host is a fresh install, workgroup
(not domain-joined), with no extra local accounts beyond the built-in
Administrator, and FastFiles installed once under its normal virtual service
account.

| Host | Role |
| --- | --- |
| Windows 11 24H2 (x64) | Primary matrix target; the supported client SKU. |
| Windows 10 22H2 (x64) | Secondary target; confirms the result is not 11-only. |
| Windows Server 2022 (x64) | Secondary target; confirms server-core/full result parity. |

A matrix run on the primary target is the minimum acceptable evidence; the
secondary targets are run before any candidate is promoted to production to
rule out a version-specific pass.

## NTFS volumes evaluated per host

| Volume | Device path | Why |
| --- | --- | --- |
| System volume | `\\.\C:` | The path the diagnosis observed failing; the MFT/USN source the scan path must read. |
| Secondary fixed NTFS volume | `\\.\D:` (when present) | The original `index-storage-and-scanning` 4.6 failure was on `\\.\D:`; confirms the result is not system-volume-specific. |

If a host has only one fixed NTFS volume, the `\\.\D:` row is recorded as
`SKIPPED(no-secondary-volume)` rather than omitted, so the matrix shape is
stable across hosts.

## Candidate rows

`control = yes` rows are **diagnostic-only** and MUST NOT become the production
model without the explicit security decision above. Each non-control row
preserves the existing signed-image and authorization-group boundary; only the
granted right(s) and/or group membership change.

| # | candidateId | identity | granted right(s) | group membership | control | rationale |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `baseline-sebackup` | `NT SERVICE\FastFilesIndexSvc` | `SeBackupPrivilege` | none | no | Reproduces the failed baseline (diagnosis verdict: `enabled-but-denied`). Reference row that anchors the matrix to the known-bad state. |
| 2 | `sebackup-restore` | `NT SERVICE\FastFilesIndexSvc` | `SeBackupPrivilege` + `SeRestorePrivilege` | none | no | `SeRestorePrivilege` is `SeBackupPrivilege`'s documented counterpart (Restore files and directories); tests whether the paired grant changes the device open. |
| 3 | `semanage-volume` | `NT SERVICE\FastFilesIndexSvc` | `SeManageVolumePrivilege` | none | no | Documented user right "Perform volume maintenance tasks"; the closest documented right to volume-handle access. |
| 4 | `sebackup-semanage` | `NT SERVICE\FastFilesIndexSvc` | `SeBackupPrivilege` + `SeManageVolumePrivilege` | none | no | Tests whether SeBackup's ACL bypass combined with volume maintenance opens the device. |
| 5 | `se-security` | `NT SERVICE\FastFilesIndexSvc` | `SeSecurityPrivilege` | none | no | Grants `ACCESS_SYSTEM_SECURITY` (SACL access); included for completeness — expected not to open the device, which is itself useful negative evidence. |
| 6 | `backup-operators` | `NT SERVICE\FastFilesIndexSvc` | `SeBackupPrivilege` | Backup Operators (`S-1-5-32-551`) | no | Built-in group whose members can back up/restore files regardless of ACL; tests whether **group membership** (vs the privilege alone) changes the device open — the privilege-vs-group distinction the diagnosis could not resolve. |
| 7 | `control-administrators` | `NT SERVICE\FastFilesIndexSvc` | `SeBackupPrivilege` | Administrators (`S-1-5-32-544`) | **yes** | Establishes the "works because admin" ceiling. The original task-4.6 run only passed under an elevated identity; this row quantifies that ceiling. Never production without the explicit security decision. |
| 8 | `control-localsystem` | `LocalSystem` | (all, inherently) | SYSTEM (`S-1-5-18`) | **yes** | Establishes the LocalSystem ceiling — the identity under which task 4.6's open succeeded. Last-resort only; selecting it triggers the constrained-broker fallback design decision. |

Privilege display-name strings (`SeBackupPrivilege`, `SeRestorePrivilege`,
`SeManageVolumePrivilege`, `SeSecurityPrivilege`) are the values
`LsaAddAccountRights` accepts and match the `SE_*_NAME` constants used in
`PrivilegeVerification.cpp`; per Microsoft's Privilege Constants reference,
`SeBackupPrivilege` grants read access to "any file, regardless of the ACL
specified for the file" — notably **files/directories**, not raw volume device
objects, which is consistent with the observed denial. No documented narrow
privilege explicitly grants a raw *device* open; whether any of rows 2–6
nonetheless passes is exactly what task 1.3 determines empirically.

## What every row records

Per spec scenario "Candidate matrix captures the real token", each row is
captured by `CaptureCandidateMatrixRow` / `LogCandidateMatrixRow`
(`src/indexsvc/src/PrivilegeVerification.cpp`, task 1.2) and persisted as a
structured `ffprotocol::DiagnosticEvent` (`state=candidate-matrix`). The
recorded fields are:

| Evidence | Source | Harness field |
| --- | --- | --- |
| Candidate identifier | matrix row | `candidateId`, `regOrder` |
| Service account SID | `GetTokenInformation(TokenUser)` | `accountSid` |
| Service account name | `LookupAccountSidW` | `account` |
| Held/enabled privilege state (SeBackup, SeRestore, SeManageVolume, SeSecurity) | `GetTokenInformation(TokenPrivileges)` | `privilegeName`, `privilegeHeld`, `privilegeEnabled` |
| Group context (Administrators / LocalSystem / Backup Operators / Authenticated Users) | `GetTokenInformation(TokenGroups)` → `ClassifyTokenGroups` | `group` (`admins=..;system=..;backupOps=..;authUsers=..`) |
| Raw-volume `CreateFileW` result | `CreateFileW` with `FILE_FLAG_BACKUP_SEMANTICS` | `outcome`, `error` (exact Win32 code) |
| USN journal control result | `FSCTL_QUERY_USN_JOURNAL` | `journalQueried`, `journalError` |
| USN journal read result | `FSCTL_READ_USN_JOURNAL` (minimal, non-blocking) | `journalRead`, `journalReadError` |
| Service registration/startup order | matrix runner (task 1.3) | `regOrder` |

The classified `outcome` is one of: `privilege-absent`,
`privilege-present-not-enabled`, `open-denied`, `open-succeeded-journal-failed`,
`open-succeeded-journal-succeeded` (`CandidateMatrixOutcome`).

## Pass criteria and selection (task 1.4)

A candidate **passes the privilege boundary** when, on a clean host under the
candidate's token, the raw-volume `CreateFileW` succeeds (`outcome` reaches
`open-succeeded-*`). The USN control/read results refine this:

- `open-succeeded-journal-succeeded` — the candidate fully supports the scan
  path; preferred.
- `open-succeeded-journal-failed` with `journalError`/`journalReadError` equal
  to a **documented non-privilege condition** (e.g. `ERROR_JOURNAL_NOT_ACTIVE`
  `0xE8`, `ERROR_JOURNAL_DELETE_IN_PROGRESS`) — the privilege boundary is
  sufficient; the journal simply is not active on the test volume. This is a
  pass for the privilege decision (consistent with
  `VerifyBackupPrivilegeSufficiency`'s existing treatment of
  `ERROR_JOURNAL_NOT_ACTIVE` as orthogonal to the open).
- `open-succeeded-journal-failed` with any other code — inconclusive; re-run
  on a host with an active USN journal before selecting.
- `open-denied` / `privilege-*` — the candidate does not pass.

The selected production model is the **narrowest** passing non-control row, in
preference order: `baseline-sebackup` (already known to fail) → `sebackup-restore`
→ `semanage-volume` → `sebackup-semanage` → `se-security` → `backup-operators`.
If no non-control row passes, the evidence identifies the constrained-broker
security decision required (design.md fallback boundary), and the
`control-administrators` / `control-localsystem` rows quantify the ceiling that
broker would operate under — but neither control becomes production.

## Provisioning notes (implemented in tasks 1.3 / 2.x, not here)

- **Right grants** (rows 1–5): `LsaAddAccountRights` against the
  `NT SERVICE\FastFilesIndexSvc` SID with the row's privilege display name(s),
  mirroring the existing `GrantBackupPrivilegeToVirtualAccount` in
  `src/setup/src/ServiceRegistration.cpp`.
- **Group membership** (rows 6–7): `NetLocalGroupAddMembers` adding the
  service SID to Backup Operators / Administrators (service SIDs
  `S-1-5-80-...` are valid local-group member SIDs).
- **Identity swap** (row 8): `ChangeServiceConfig` to `LocalSystem` for the
  control run, then restored.
- **Fresh token**: after any grant or identity change, the service is
  restarted before the row is captured, so the tested token is the token the
  real scan path uses (spec scenario "Restart uses the newly granted token").
- **Transactional rollback**: a failed grant or failed post-grant verification
  rolls back the partial registration (spec scenario "Grant failure prevents
  an invalid service start"); implemented in task 2.2.

## Reproducibility

A matrix run is reproducible when, given the same host image and the same
ordered candidate list above, re-running produces the same `outcome`, exact
Win32 error codes, account SID, and group context for every row. Task 1.3
attaches the run's structured log (the `candidate-matrix` events) and a
machine/host fingerprint to the change evidence without altering the
production installation.
