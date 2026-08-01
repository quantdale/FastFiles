## ADDED Requirements

### Requirement: Real call-site privilege capture
`FastFilesIndexSvc` SHALL capture the process token's privilege state (each relevant privilege's held/enabled status) and the token's account identity immediately before the raw NTFS volume `CreateFileW` call in the real volume-scanning code path, not only in a separate synthetic probe path.

#### Scenario: Capture runs on the real scan path
- **WHEN** `FastFilesIndexSvc` opens a raw volume handle as part of a real `StartVolumeScan` request
- **THEN** the service SHALL record, for that exact call, whether `SeBackupPrivilege` was present in the token, whether it was enabled, and the account SID/name the token represents

### Requirement: Distinguishable failure classification
The diagnostic capture SHALL classify the outcome of a raw-volume-open attempt into one of the following distinct categories, and SHALL NOT report a bare pass/fail without this classification: privilege absent from token; privilege present but not enabled; privilege enabled but the open call was denied; open call succeeded.

#### Scenario: Privilege present but not enabled
- **WHEN** the token holds `SeBackupPrivilege` per `GetTokenInformation(TokenPrivileges)` but the privilege's `Attributes` does not include `SE_PRIVILEGE_ENABLED`
- **THEN** the diagnostic record SHALL classify the outcome as "privilege present but not enabled" rather than merging it with "open call denied"

#### Scenario: Privilege enabled but the open call is denied
- **WHEN** the token holds `SeBackupPrivilege` with `SE_PRIVILEGE_ENABLED` set and the subsequent `CreateFileW` on the raw volume path still returns `ERROR_ACCESS_DENIED`
- **THEN** the diagnostic record SHALL classify the outcome as "privilege enabled but open call was denied," distinct from any privilege-state defect

### Requirement: Evidence persists beyond a single console session
The diagnostic capture SHALL be recorded as a structured log entry that survives the process's lifetime (e.g., written to a local diagnostic log or existing verification-evidence path), not only emitted to the console/stderr of an interactive run.

#### Scenario: Evidence available after the service process exits
- **WHEN** a raw-volume-open attempt is captured during a real service run and the service process later exits or is restarted
- **THEN** the structured diagnostic record from that attempt SHALL remain readable from disk

### Requirement: No behavior change when diagnostics are inert
Adding this diagnostic capture SHALL NOT alter the success or failure outcome of the underlying `CreateFileW`/`DeviceIoControl` calls it observes — it SHALL be strictly observational until an explicit, separately-decided configuration fix is applied.

#### Scenario: Diagnostic-only run reproduces the existing outcome
- **WHEN** the diagnostic capture is added with no accompanying privilege/configuration fix
- **THEN** a raw-volume-open attempt that previously succeeded SHALL still succeed, and one that previously failed with `ERROR_ACCESS_DENIED` SHALL still fail with the same error, under otherwise identical conditions
