# privileged-index-service Specification

## Purpose
The privileged broker contract: a stateless service with a closed command surface, opaque connection-scoped handles, symmetric mutual authentication, hardened frame validation, and real MFT/USN scanning with parser-level attribute allowlisting.

## Requirements
### Requirement: Minimal Privilege Grant
**Superseded:** This requirement's privilege-minimization premise is superseded by the evidence-backed constrained-broker decision in `resolve-raw-volume-privilege-insufficiency` (evidence: `openspec/changes/resolve-raw-volume-privilege-insufficiency/evidence/matrix-execution-and-selection.md`): no narrow user right or group membership opens a raw volume device on modern Windows, so `FastFilesIndexSvc` runs under **LocalSystem** as a deliberately constrained privileged broker. The original requirement text is retained below as the superseded statement; its scenario remains valid only for the non-production virtual-account registration path.

`FastFilesIndexSvc` SHALL run under a dedicated virtual service account and SHALL be granted `SeBackupPrivilege` only. It SHALL NOT run as `LocalSystem`, SHALL NOT be a member of Administrators, and SHALL NOT be granted `SeRestorePrivilege`, `SeDebugPrivilege`, or `SeTcbPrivilege`.

#### Scenario: Service installation grants only SeBackupPrivilege
- **WHEN** the installer registers `FastFilesIndexSvc` with the Service Control Manager
- **THEN** the service's virtual account SHALL have exactly `SeBackupPrivilege` granted via `LsaAddAccountRights` and no other sensitive privilege

### Requirement: Stateless Operation
`FastFilesIndexSvc` SHALL hold no filesystem index and SHALL persist no cross-restart state beyond its own configuration. All index data SHALL live exclusively in `FastFilesEngine`.

#### Scenario: Service restart requires no state recovery
- **WHEN** `FastFilesIndexSvc` restarts for any reason
- **THEN** it SHALL come up with no persisted index, query, or session state to recover, and SHALL be ready to accept new connections immediately

### Requirement: Closed Command Protocol
The engine-to-service command surface SHALL be a fixed, closed set: `Handshake`, `EnumerateVolumes`, `StartVolumeScan(VolumeId)`, `OpenUsnJournal(VolumeId, ResumeUsn)`, `StopVolumeScan(VolumeId)`, `CloseUsnJournal(VolumeId)`, `Heartbeat`. No message type accepting an arbitrary client-supplied path, handle, or device string SHALL exist.

#### Scenario: Unrecognized message type rejected
- **WHEN** a connected client sends a frame whose `MessageType` is not one of the defined commands
- **THEN** the service SHALL reject the frame and close that connection rather than attempting to interpret it

#### Scenario: Volume enumeration is service-controlled
- **WHEN** a client calls `EnumerateVolumes`
- **THEN** the service SHALL return only fixed local NTFS/ReFS volumes it discovers itself, and SHALL NOT accept a client-supplied path or device string as an alternative way to select a volume

### Requirement: Opaque, Connection-Scoped Handles
`VolumeId` and `JournalId` values SHALL be opaque and service-assigned. Each open scan or journal handle SHALL be scoped to the connection that created it.

#### Scenario: Stop request from a different connection is rejected
- **WHEN** a connection that did not call `StartVolumeScan` for a given `VolumeId` sends `StopVolumeScan` for that same `VolumeId`
- **THEN** the service SHALL reject the request

#### Scenario: Disconnect tears down owned scans and journals
- **WHEN** a connection closes (cleanly or unexpectedly) while it has open scans or journals
- **THEN** the service SHALL tear down that connection's scans/journals without requiring an explicit `Stop`/`Close` message

### Requirement: Symmetric Mutual Authentication
The service SHALL verify each connecting client's process image path (under the ACL-protected install directory) and pinned Authenticode signature, in addition to checking Windows group membership. This verification SHALL be re-checked periodically for long-lived connections, not only at initial connect.

#### Scenario: Connection from an unverified binary is rejected
- **WHEN** a process that belongs to the authorized group but is not the genuine, signed `FastFilesEngine.exe` (wrong image path or signature) connects and completes `Handshake`
- **THEN** the service SHALL reject any further command beyond the handshake rejection itself

#### Scenario: Revoked membership closes an active connection
- **WHEN** a previously-authorized connection's user is removed from the authorized group while the connection is still open
- **THEN** the service SHALL detect this on its next periodic re-validation and close the connection

### Requirement: No Client-Grantable Service Control Rights
The unprivileged client group SHALL be granted only `SERVICE_QUERY_STATUS` and `SERVICE_QUERY_CONFIG` on the service's Service Control Manager object. It SHALL NOT be granted `SERVICE_START`, `SERVICE_STOP`, `SERVICE_CHANGE_CONFIG`, `WRITE_DAC`, or `WRITE_OWNER`.

#### Scenario: Unprivileged client cannot stop the service
- **WHEN** a process running as a member of the unprivileged client group calls `ControlService` requesting `SERVICE_CONTROL_STOP` (or `StartService`) against `FastFilesIndexSvc`
- **THEN** the call SHALL fail with an access-denied error

### Requirement: Self-Directed Staleness Recovery
The service SHALL detect when its on-disk binary differs from the one it loaded at startup (via periodic timer and opportunistically at handshake) and SHALL recover by terminating itself abnormally so that the Service Control Manager's configured failure actions restart it — without relying on any externally-triggered stop/start call.

#### Scenario: Stale binary triggers self-restart
- **WHEN** the on-disk service binary has been replaced (e.g., by an update) while the running process is still the old version
- **THEN** the running service SHALL detect the mismatch within one staleness-check interval and terminate itself so SCM's failure actions bring up the new binary, without any client having invoked `SERVICE_STOP`

### Requirement: Frame and Input Validation
Every inbound frame SHALL be validated against a protocol-wide maximum frame size (checked before any allocation, using arithmetic wider than the size field itself) before parsing. Batch record counts SHALL be cross-validated against the actual payload byte count before any record is parsed. Malformed, oversized, or inconsistent frames SHALL cause the connection to be closed, not a crash or best-effort continuation.

#### Scenario: Oversized frame is rejected
- **WHEN** a frame header declares a `TotalLength` exceeding the protocol-wide maximum
- **THEN** the receiving side SHALL reject the frame and close the connection before allocating a buffer sized from that value

#### Scenario: Record count mismatch is rejected
- **WHEN** a batch frame's declared record count, multiplied by the fixed record size, does not equal its payload size
- **THEN** the receiving side SHALL reject the entire frame rather than parsing any record from it

### Requirement: Named Pipe Access Control
Both IPC pipes SHALL be created with an explicit security descriptor restricted to the authorized client group and `SYSTEM` — never a null DACL or `Everyone`/`Authenticated Users` — and SHALL be created with `FILE_FLAG_FIRST_PIPE_INSTANCE`.

#### Scenario: Pipe-name collision fails loudly
- **WHEN** a pipe of the service's expected name already exists at service startup (e.g., created by another process)
- **THEN** the service's own `CreateNamedPipe` call SHALL fail, and the service SHALL log this condition and refuse to proceed rather than silently operating without its expected pipe

### Requirement: Real MFT Volume Scanning and USN Journal Streaming
`FastFilesIndexSvc` SHALL perform real raw MFT parsing in response to `StartVolumeScan` and real USN Change Journal reads in response to `OpenUsnJournal`, streaming batched records to the requesting connection, rather than the "not yet implemented" stub response defined in `establish-architecture-foundation`. This requirement supersedes and replaces that change's "Scan and Journal Operations Are Explicitly Stubbed" requirement. Both operations SHALL continue to respond within the service's normal response time and SHALL NOT hang or leave the requesting connection waiting indefinitely, and SHALL parse and forward only the allowlisted fields already established (`$STANDARD_INFORMATION`, `$FILE_NAME` — never `$DATA` content).

#### Scenario: Scan request streams real MFT-derived records
- **WHEN** a client sends `StartVolumeScan` for a valid, service-enumerated `VolumeId`
- **THEN** the service SHALL open the volume using its `SeBackupPrivilege`-granted raw access, read MFT records directly, and stream batches of allowlisted-field records (`FileReferenceNumber`, `ParentFileReferenceNumber`, name, size, timestamps, attributes) to the requesting connection until the scan completes or is stopped

#### Scenario: Journal open streams real change records from a given resume point
- **WHEN** a client sends `OpenUsnJournal` for a valid `VolumeId` with a `ResumeUsn`
- **THEN** the service SHALL read the volume's USN Change Journal starting at that position (via `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL`) and stream batches of allowlisted-field change records to the requesting connection as they occur

#### Scenario: Scanning proceeds despite files locked by other processes
- **WHEN** a raw MFT scan encounters a file or directory that is exclusively locked by another process
- **THEN** the service's `SeBackupPrivilege`-based raw volume access SHALL still be able to read that record's allowlisted metadata, since raw MFT reads do not require opening the file through the normal sharing-mode path

#### Scenario: Scan and journal calls remain responsive when the volume has no data yet to report
- **WHEN** `StartVolumeScan` or `OpenUsnJournal` is called on a volume with no new records currently available
- **THEN** the service SHALL respond within its normal response time (an empty or pending-batch acknowledgment), never blocking the connection indefinitely waiting for data to appear

### Requirement: MFT Attribute Allowlisting Enforced at the Parser
The MFT parser SHALL read and forward only the allowlisted attribute types (`$STANDARD_INFORMATION`, `$FILE_NAME`) for each record. Any other attribute type present in a record, including `$DATA`, SHALL be ignored and SHALL NOT be forwarded to the requesting connection under any circumstance, including for small files whose content is resident inside the MFT record itself.

#### Scenario: Resident file content is never forwarded
- **WHEN** a scanned MFT record has resident `$DATA` content stored inline (a small file)
- **THEN** the service SHALL forward only that record's `$STANDARD_INFORMATION`/`$FILE_NAME` fields and SHALL NOT include any `$DATA` bytes in the streamed record

#### Scenario: Non-allowlisted attribute type is dropped, not forwarded
- **WHEN** an MFT record contains an attribute type outside the allowlist (e.g., `$SECURITY_DESCRIPTOR`, `$REPARSE_POINT` payload bytes, alternate data streams)
- **THEN** the service SHALL exclude that attribute's content entirely from the streamed record without treating its presence as an error

### Requirement: Malformed MFT or USN Records Are Isolated, Not Fatal
A single malformed or internally inconsistent MFT record or USN change record encountered mid-scan or mid-journal-read SHALL be skipped and logged, and SHALL NOT abort the remainder of the scan or journal stream, provided the record itself passes the protocol-level frame and length validation already required. This is distinct from frame-level validation (which governs wire-format integrity); this requirement governs semantic validity of an individual on-disk NTFS record once its frame has already been accepted.

#### Scenario: One corrupt record does not stop the scan
- **WHEN** the service encounters an MFT record with internally inconsistent fields (e.g., a `$FILE_NAME` attribute whose declared name length does not fit within the record) while scanning a volume
- **THEN** the service SHALL skip that single record, continue scanning subsequent records, and SHALL NOT close the connection or abort the scan solely because of that one record

### Requirement: USN Journal Identity Reported for Resume Validation
`OpenUsnJournal` responses SHALL include the volume's current USN Journal identifier (`JournalId`) alongside streamed records, so the caller can detect when a previously-persisted `ResumeUsn` is no longer valid for the journal now present on the volume.

#### Scenario: Journal identity is included so the caller can detect a recreated journal
- **WHEN** a client calls `OpenUsnJournal` with a `ResumeUsn` obtained from a previous session
- **THEN** the service SHALL report the volume's current `JournalId` to the caller, allowing the caller to determine whether that `ResumeUsn` still applies to the same journal instance or belongs to a journal that has since been deleted and recreated

### Requirement: Scan Resumption from a Caller-Supplied Cursor
`StartVolumeScan` SHALL accept an optional, opaque scan-progress cursor previously issued by the service, and SHALL resume MFT enumeration from approximately that point rather than always restarting from the beginning of the MFT, as an additive extension to the existing closed command surface.

#### Scenario: Scan resumes from a supplied cursor instead of record zero
- **WHEN** a client calls `StartVolumeScan` for a `VolumeId` and supplies a previously-issued scan-progress cursor
- **THEN** the service SHALL resume MFT enumeration from approximately the position that cursor represents rather than re-enumerating the volume's MFT from its start

#### Scenario: Omitted cursor performs a full scan
- **WHEN** a client calls `StartVolumeScan` without supplying a scan-progress cursor
- **THEN** the service SHALL perform a full MFT enumeration from the beginning, exactly as it would for a first-time scan of that volume
