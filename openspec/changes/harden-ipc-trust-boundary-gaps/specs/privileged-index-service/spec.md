## MODIFIED Requirements

### Requirement: Symmetric Mutual Authentication
The service SHALL verify each connecting client's process image path (under the ACL-protected install directory) and pinned Authenticode signature, in addition to checking Windows group membership. The engine SHALL verify the service's process image path and pinned Authenticode signature before trusting the connection. Both verifications SHALL be re-checked periodically for long-lived connections, not only at initial connect. A periodic re-validation failure SHALL close that connection.

#### Scenario: Connection from an unverified binary is rejected
- **WHEN** a process that belongs to the authorized group but is not the genuine, signed `FastFilesEngine.exe` (wrong image path or signature) connects and completes `Handshake`
- **THEN** the service SHALL reject any further command beyond the handshake rejection itself

#### Scenario: Revoked membership closes an active connection
- **WHEN** a previously-authorized connection's user is removed from the authorized group while the connection is still open
- **THEN** the service SHALL detect this on its next periodic re-validation and close the connection

#### Scenario: Engine detects a replaced service binary on a live connection
- **WHEN** an Active engine↔service connection is in the heartbeat cadence and the service image path or pinned signature no longer verifies
- **THEN** the engine SHALL close that connection and transition to Disconnected rather than remaining Active

### Requirement: Named Pipe Access Control
Both IPC pipes SHALL be created with an explicit security descriptor restricted to the authorized client group and `SYSTEM` for the elevation-seam pipe, and to the current user (plus `SYSTEM`/Administrators as already implemented) for the same-privilege UI pipe — never a null DACL or `Everyone`/`Authenticated Users`. Every instance SHALL be created with `FILE_FLAG_FIRST_PIPE_INSTANCE` on the first instance of that name. Every instance SHALL also set `PIPE_REJECT_REMOTE_CLIENTS` so SMB/remote named-pipe clients cannot connect.

#### Scenario: Pipe-name collision fails loudly
- **WHEN** a pipe of the service's expected name already exists at service startup (e.g., created by another process)
- **THEN** the service's own `CreateNamedPipe` call SHALL fail, and the service SHALL log this condition and refuse to proceed rather than silently operating without its expected pipe

#### Scenario: Remote named-pipe client is rejected
- **WHEN** a remote SMB client attempts to open `\\<host>\pipe\FastFiles.Svc.Ctrl` or the per-session UI control pipe
- **THEN** the create/connect SHALL fail at the pipe mode (`PIPE_REJECT_REMOTE_CLIENTS`) rather than reaching handshake or UI Subscribe
