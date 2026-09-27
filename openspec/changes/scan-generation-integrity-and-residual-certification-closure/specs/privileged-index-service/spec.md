## ADDED Requirements

### Requirement: Scan supersede stays inside the closed command surface

The privileged service SHALL keep the closed engine-to-service command surface defined by `establish-architecture-foundation`. This capability SHALL NOT add a command that accepts a client-supplied path, handle, or device string. When a new scan supersedes an older scan of the same connection-scoped volume id, a stop that is observed before the terminal is written SHALL NOT emit scan completion or scan failure for the stopped attempt. The existing completion and failure payloads SHALL remain exact-length volume-id messages. An attempt discriminator, if one is introduced after in-process retirement is shown to be insufficient, SHALL be a distinct message type that a peer which does not negotiate it cannot parse as scan completion or scan failure.

#### Scenario: A stop observed before the terminal emits nothing

- **WHEN** the service stops a scan worker because the same connection starts another scan of that volume id, and the stop flag is visible before the worker writes a terminal
- **THEN** the service SHALL NOT write scan completion
- **AND** SHALL NOT write scan failure for that stopped attempt

#### Scenario: Completion and failure payloads are not resized in place

- **WHEN** a peer parses scan completion or scan failure
- **THEN** each of those existing messages SHALL still be exactly the connection-scoped volume id
- **AND** a longer payload on either message SHALL be a protocol violation

#### Scenario: No open-path command is added

- **WHEN** a client sends a message that asks the service to open a caller-chosen path, handle, or device
- **THEN** the service SHALL reject the message and close that connection
- **AND** the closed command set SHALL NOT gain such a message as part of scan-attempt tracking

#### Scenario: A later attempt discriminator cannot be mistaken for completion

- **WHEN** an attempt-discriminator message is added because a volume-id-only terminal cannot be ordered in process
- **THEN** it SHALL use a new message type
- **AND** a peer that does not negotiate that type SHALL NOT accept it as scan completion
