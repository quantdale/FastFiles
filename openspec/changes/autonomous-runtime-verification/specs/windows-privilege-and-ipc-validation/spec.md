## ADDED Requirements

### Requirement: Privilege And Token Validation
The harness SHALL verify the privilege and token posture of the running processes: that `FastFilesIndexSvc` holds `SeBackupPrivilege`/`SeRestorePrivilege` as designed and no more, and that service, user, administrator, standard-user, and SYSTEM contexts and integrity levels are as expected for each process.

#### Scenario: SeBackupPrivilege alone suffices for privileged access
- **WHEN** the service performs its privileged raw-volume/journal access path holding only `SeBackupPrivilege` (non-admin token)
- **THEN** the harness SHALL confirm the access succeeds, validating the privilege-minimization design, and SHALL record the result

#### Scenario: Process integrity levels match expectations
- **WHEN** the harness inspects the running service, engine, and UI processes
- **THEN** each process's token privileges and integrity level SHALL match the expected posture, and any deviation SHALL be reported with a diagnostic

### Requirement: ACL And Object Security Validation
The harness SHALL verify the ACLs/security descriptors on FastFiles-created securable objects — install directory, named pipes, and shared-memory mappings — match the intended access model.

#### Scenario: Named-pipe and shared-memory ACLs are correct
- **WHEN** the harness inspects a FastFiles named pipe and snapshot shared-memory mapping
- **THEN** their security descriptors SHALL grant only the intended principals the intended access, and any over-permissive grant SHALL be flagged as a failure

### Requirement: Authenticode Verification And Failure Diagnostics
The harness SHALL verify Authenticode signatures on the FastFiles binaries and SHALL, on any privilege/ACL/signature failure, generate a diagnostic explaining what was expected, what was observed, and the likely cause.

#### Scenario: A signature or ACL failure yields an explanatory diagnostic
- **WHEN** an Authenticode, ACL, or privilege check fails
- **THEN** the harness SHALL emit a diagnostic stating the expected value, the observed value, and a plausible cause rather than only a pass/fail flag

### Requirement: Engine–Service Connection Validation
The harness SHALL validate the engine↔service relationship: service discovery, mutual authentication with Authenticode pinning, heartbeats, connection recovery, idle disconnect, wire/build version compatibility, and startup sequencing.

#### Scenario: A non-genuine engine is rejected at handshake
- **WHEN** a process that is not the genuine `FastFilesEngine.exe` (wrong image path or signature) connects to the privileged pipe
- **THEN** the service SHALL reject it at handshake and the harness SHALL record the rejection as the expected pass condition

#### Scenario: Heartbeat loss triggers recovery
- **WHEN** an active engine↔service connection stops producing heartbeats within the timeout
- **THEN** the harness SHALL observe the connection force-close and recover (reconnect), and SHALL record the transition

#### Scenario: Version mismatch drops to degraded mode
- **WHEN** the engine and service report incompatible build or wire-protocol versions
- **THEN** the harness SHALL confirm the engine treats the privileged path as unavailable and drops to degraded mode with an actionable status

### Requirement: Named-Pipe And Shared-Memory IPC Validation
The harness SHALL validate the IPC seam: pipe creation with first-instance protection, pipe ACLs, the authentication handshake, shared-memory snapshot creation, snapshot publication and synchronization, session isolation, timeout recovery, and reconnection.

#### Scenario: Pipe squatting fails loudly
- **WHEN** a pipe of the expected name already exists and the service attempts to create its instance with first-instance protection
- **THEN** creation SHALL fail with a clear, logged error, and the harness SHALL record that hard failure as the expected pass condition

#### Scenario: A published snapshot is readable without a round trip
- **WHEN** the engine publishes a snapshot generation and notifies a subscribed client
- **THEN** the harness SHALL confirm the client can read the already-published data from the shared mapping without an additional IPC request, and that the generation matches

#### Scenario: Session isolation holds across sessions
- **WHEN** engine/service instances run under session-suffixed pipe and mapping names in more than one context available to the run
- **THEN** the harness SHALL confirm one session's client cannot read another session's pipe/mapping

### Requirement: Protocol Robustness Validation
The harness SHALL validate protocol robustness against malformed packets, out-of-range length-prefixed fields, record-count mismatches, and large payloads, confirming the parser rejects bad input without crashing and bounds allocation before reading.

#### Scenario: Malformed frames are rejected, not fatal
- **WHEN** malformed or oversized frames are delivered to the parser
- **THEN** the harness SHALL confirm they are rejected safely (no crash, no over-allocation) and SHALL record the outcome

#### Scenario: A large valid payload is handled within bounds
- **WHEN** a large but valid payload up to the protocol maximum is delivered
- **THEN** the harness SHALL confirm it is processed correctly and that a payload exceeding the maximum is refused before allocation
