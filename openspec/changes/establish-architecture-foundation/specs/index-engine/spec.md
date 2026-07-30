## ADDED Requirements

### Requirement: Privileged Connection Lifecycle State Machine
`FastFilesEngine` SHALL manage its connection to `FastFilesIndexSvc` as an explicit state machine (`Disconnected → Connecting → Handshaking → Active`), where any failure at any stage — including a live disconnect from `Active` — transitions back to `Disconnected`.

#### Scenario: Connection failure returns to Disconnected
- **WHEN** the engine's connection to the service breaks or a handshake fails for any reason
- **THEN** the engine's state SHALL transition to `Disconnected` and the engine SHALL attempt reconnection using exponential backoff rather than a tight retry loop

#### Scenario: Heartbeat timeout is treated as disconnection
- **WHEN** an `Active` connection produces no heartbeat response within a defined timeout, even though the underlying pipe has not reported an I/O error
- **THEN** the engine SHALL force-close the connection and transition to `Disconnected`, rather than remaining in `Active` indefinitely

### Requirement: Degraded-Mode Directory Enumeration
When not in the `Active` state, `FastFilesEngine` SHALL enumerate directory contents using unprivileged Win32 APIs, respecting the requesting user's own filesystem permissions, and SHALL treat this as a fully supported, permanent operating mode rather than an error condition.

#### Scenario: Directory listing succeeds with the privileged service unavailable
- **WHEN** `FastFilesIndexSvc` is not installed, not running, or not yet connected
- **THEN** `FastFilesEngine` SHALL still be able to enumerate and return the contents of a directory the user has permission to read

#### Scenario: Inaccessible subfolder is skipped, not fatal
- **WHEN** a directory enumeration encounters a subfolder the current user's token cannot access
- **THEN** the engine SHALL omit that subfolder's contents (or mark it as inaccessible) and continue enumerating the rest of the directory without failing the whole operation

### Requirement: Change Detection for Browsed Directories
While in degraded mode, `FastFilesEngine` SHALL maintain a `ReadDirectoryChangesW` watch for each directory a user has browsed or explicitly pinned, so that changes are reflected without requiring a full manual re-enumeration.

#### Scenario: A newly created file appears without manual refresh
- **WHEN** a file is created in a directory currently being watched by the engine
- **THEN** the engine SHALL detect the change and make the updated listing available to subscribed UI clients without the user needing to trigger a manual refresh

### Requirement: Filesystem Snapshot Publication
`FastFilesEngine` SHALL publish its current view of enumerated directories to UI clients as an immutable, read-only snapshot accessible via a shared memory mapping, and SHALL notify subscribed clients of new snapshot generations over a control IPC channel, rather than requiring clients to make a round-trip IPC call to read already-published data.

#### Scenario: UI reads already-published data without an IPC round trip
- **WHEN** a UI client has mapped the current snapshot generation
- **THEN** reading the contents of a directory already reflected in that snapshot SHALL require no IPC call to the engine

#### Scenario: New snapshot generation is announced
- **WHEN** the engine's enumerated view of a directory changes (new entry, removed entry, or an initial enumeration completing)
- **THEN** the engine SHALL publish a new snapshot generation and notify subscribed UI clients of it over the control pipe

### Requirement: Version-Aware Reconnection
`FastFilesEngine` SHALL treat a product build-version mismatch or a wire-protocol major-version mismatch with `FastFilesIndexSvc` as equivalent to a disconnected privileged path, and SHALL surface an explicit, actionable status rather than silently guessing or hanging.

#### Scenario: Incompatible protocol major version is treated as unavailable
- **WHEN** the service reports a wire-protocol major version the engine does not support
- **THEN** the engine SHALL treat the privileged path as unavailable, remain in degraded mode, and surface a non-blocking, actionable status to the UI
