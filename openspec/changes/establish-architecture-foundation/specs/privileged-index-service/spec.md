## ADDED Requirements

### Requirement: Minimal Privilege Grant
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

### Requirement: Scan and Journal Operations Are Explicitly Stubbed
For this change, `StartVolumeScan` and `OpenUsnJournal` SHALL respond with a well-defined "not yet implemented" status rather than performing real MFT parsing or USN journal reads, and SHALL NOT hang or leave the requesting connection waiting indefinitely.

#### Scenario: Scan request returns a defined not-implemented response
- **WHEN** a client sends `StartVolumeScan` for a valid, service-enumerated `VolumeId`
- **THEN** the service SHALL reply with an explicit not-yet-implemented status within its normal response time, allowing the caller to treat this identically to how it would treat a genuinely degraded/unavailable service
