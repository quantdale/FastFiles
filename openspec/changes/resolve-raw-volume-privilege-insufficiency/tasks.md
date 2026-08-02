## 1. Establish the privilege evidence matrix

- [x] 1.1 Define the supported Windows hosts, NTFS volumes, and documented account/right candidates to evaluate, including a LocalSystem or Administrators control that is explicitly marked diagnostic-only. (Defined in `evidence/candidate-matrix.md`: 3 supported Windows hosts (Win11 24H2 primary, Win10 22H2 + Server 2022 secondary), system + secondary NTFS volumes, 8 candidate rows — 6 narrow privilege/group candidates (`baseline-sebackup`, `sebackup-restore`, `semanage-volume`, `sebackup-semanage`, `se-security`, `backup-operators`) plus `control-administrators` and `control-localsystem` explicitly marked diagnostic-only — each mapped to the task-1.2 harness fields, with pass criteria, the documented-non-privilege-condition rule, and the least-privilege selection order.)
- [x] 1.2 Extend the diagnostic harness to record service account SID, group context, held/enabled privilege state, raw-volume `CreateFileW` results, USN journal control/read results, service registration order, and exact Win32 error codes for every candidate. (`src/indexsvc/src/PrivilegeVerification.{h,cpp}`: `CaptureTokenPrivilegeState` now captures the candidate privilege set (SeBackup/SeRestore/SeManageVolume/SeSecurity) held+enabled state plus well-known group context (Administrators/LocalSystem/Backup Operators/Authenticated Users) via the new pure `ClassifyTokenGroups`; new `CandidateMatrixRow`/`CaptureCandidateMatrixRow`/`LogCandidateMatrixRow` enable the named candidate privilege, open the raw volume with `FILE_FLAG_BACKUP_SEMANTICS`, run `FSCTL_QUERY_USN_JOURNAL` plus a minimal non-blocking `FSCTL_READ_USN_JOURNAL`, and classify via the pure `ClassifyCandidateMatrixRow` (5 outcomes) / `CandidateMatrixOutcomeName` / `FormatGroupContext`; `EnsurePrivilegeEnabled` generalizes the SeBackup enable. `src/protocol/.../Diagnostics.{h,cpp}`: `DiagnosticEvent` gained `candidateId`/`privilegeName`/`groupContext`/`journalQueried`/`journalRead` + their exact error codes / `registrationOrder`, emitted only for matrix rows (`candidateId` non-empty) so every existing diagnostic event serializes byte-identically and existing positional aggregate initializers keep working. Unit-tested in `tests/indexsvc/test_privilege_diagnostics.cpp`; full build green under `/W4 /WX /permissive-` C++20 and 18/18 `ctest` pass.)
- [ ] 1.3 Run the candidate matrix on a clean disposable host and attach the reproducible results to the change evidence without changing the production installation.
- [ ] 1.4 Select the narrowest candidate that passes the initial raw-volume and USN operations, or record the evidence and security decision required for a constrained broker when no narrow candidate passes.

## 2. Implement the selected service access model

- [ ] 2.1 Update service registration and account-right provisioning for the selected candidate while preserving the existing signed-image and authorization-group boundary.
- [ ] 2.2 Apply the service security descriptor and selected rights before starting the service, and make grant/configuration failures roll back partial registration without reporting a usable service.
- [ ] 2.3 Ensure right or identity changes force a fresh service start before verification, and expose the verified token details in the startup/scan diagnostic path.
- [ ] 2.4 If the candidate matrix requires a broker, implement a dedicated privileged boundary limited to authenticated volume enumeration, raw-volume scan/journal, cancellation, and structured-status commands.
- [ ] 2.5 Reject unauthorized broker clients before privileged work and reject commands outside the documented volume/journal surface, including arbitrary process, file-mutation, and unrestricted-handle operations.

## 3. Make deployment signed and reversible

- [ ] 3.1 Enforce non-placeholder mutual-authentication pins and signed Service/Engine artifacts before enabling the production privileged path.
- [ ] 3.2 Add transactional backup, upgrade, startup verification, and rollback for service binaries, configuration, rights, and security descriptors, restoring the prior known-good state after any failed verification.
- [ ] 3.3 Verify that the diagnostic unsigned build switch is unavailable from release artifacts and cannot weaken production installation checks.

## 4. Validate and hand off the final model

- [ ] 4.1 Build and sign the final artifacts, install them on a clean supported host, and verify service startup, recovery, and rollback behavior.
- [ ] 4.2 Run a real `StartVolumeScan` through the installed service and verify raw-volume open, normal scan publication, and USN journal query/read outcomes under the same token.
- [ ] 4.3 Attach final evidence containing account identity, privilege state, raw-volume and USN outcomes, exact errors, selected model, and rollback status.
- [ ] 4.4 Update the owning architecture and index-storage/scanning tasks only after the signed real-service evidence closes the privilege decision; otherwise leave them open with the constrained-broker decision recorded.
