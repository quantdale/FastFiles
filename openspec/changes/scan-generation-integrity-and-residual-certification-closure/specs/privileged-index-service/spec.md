## ADDED Requirements

### Requirement: Scan supersede stays inside the closed command surface

The privileged service SHALL keep the closed engine-to-service command surface defined by `establish-architecture-foundation`. This capability SHALL NOT add a command that accepts a client-supplied path, handle, or device string. When a new scan supersedes an older scan of the same connection-scoped volume id, a stop that is observed before the terminal is written SHALL NOT emit scan completion or scan failure for the stopped attempt. The existing completion and failure payloads SHALL remain exact-length volume-id messages. Protocol `{2,3}` SHALL add a service-to-engine `ScanAttemptStarted` message with an exact-length volume-id payload; the service writes it after joining the superseded worker and before launching the replacement. A peer that did not negotiate it SHALL receive no unknown reply-only message.

#### Scenario: A stop observed before the terminal emits nothing

- **WHEN** the service stops a scan worker because the same connection starts another scan of that volume id, and the stop flag is visible before the worker writes a terminal
- **THEN** the service SHALL NOT write scan completion
- **AND** SHALL NOT write scan failure for that stopped attempt

#### Scenario: Completion and failure payloads are not resized in place

- **WHEN** a peer parses scan completion or scan failure
- **THEN** each of those existing messages SHALL still be exactly the connection-scoped volume id
- **AND** a longer payload on either message SHALL be a protocol violation

#### Scenario: The scan-start barrier orders the replacement

- **WHEN** a `{2,3}` peer starts a scan that supersedes an existing worker for the same volume id
- **THEN** the service SHALL join the old worker before writing `ScanAttemptStarted`
- **AND** SHALL write that barrier before the replacement worker can write a batch or terminal
- **AND** SHALL serialize the barrier with every other service-to-engine frame on that connection

#### Scenario: An older peer does not receive an unknown barrier

- **WHEN** the negotiated peer predates `{2,3}`
- **THEN** the service SHALL NOT write `ScanAttemptStarted`
- **AND** SHALL preserve the exact existing completion and failure message layouts

#### Scenario: No open-path command is added

- **WHEN** a client sends a message that asks the service to open a caller-chosen path, handle, or device
- **THEN** the service SHALL reject the message and close that connection
- **AND** the closed command set SHALL NOT gain such a message as part of scan-attempt tracking

#### Scenario: A later attempt discriminator cannot be mistaken for completion

- **WHEN** an attempt-discriminator message is added because a volume-id-only terminal cannot be ordered in process
- **THEN** it SHALL use a new message type
- **AND** a peer that does not negotiate that type SHALL NOT accept it as scan completion
