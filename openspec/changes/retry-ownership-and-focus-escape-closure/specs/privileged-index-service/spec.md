## ADDED Requirements

### Requirement: Scan-start barrier stays reply-only and exact

The service-to-engine scan-start barrier SHALL be the existing `ScanAttemptStarted` message. It SHALL carry exactly one connection-scoped volume id and no other bytes. It SHALL NOT carry a client-controlled path, device, or handle. A client request whose message type is `ScanAttemptStarted` SHALL be rejected. A peer that negotiated a version below 2.3 SHALL NOT receive the message and SHALL NOT be required to parse it. A peer that negotiates 2.3 SHALL receive it when a replacement scan is ordered. Unsupported versions SHALL remain rejected by the existing compatibility policy. Malformed, truncated, or extended barrier payloads SHALL fail closed and SHALL NOT be treated as a start signal.

#### Scenario: A pre-2.3 peer never depends on the barrier

- **WHEN** the negotiated peer is 2.2 or another compatible version that does not include the barrier
- **THEN** the service SHALL NOT write `ScanAttemptStarted`
- **AND** the engine SHALL NOT wait for that message before applying the legacy connection-reset rule for an ambiguous replacement

#### Scenario: A 2.3 peer negotiates the barrier

- **WHEN** both sides support protocol 2.3 and negotiation completes
- **THEN** the negotiated version SHALL include the scan-start barrier
- **AND** a replacement scan on that connection SHALL be ordered with that message

#### Scenario: A malformed barrier fails closed

- **WHEN** a frame claims to be `ScanAttemptStarted` but its payload is not exactly the volume-id size, includes trailing bytes, or is not the expected message type
- **THEN** the receiver SHALL reject the frame and SHALL NOT grant replacement-scan authority from it

#### Scenario: The barrier is not a client command

- **WHEN** a client sends `ScanAttemptStarted` as a request
- **THEN** the service SHALL reject it as outside the closed command surface
- **AND** SHALL NOT open a path, device, or handle from that frame

### Requirement: Replacement output is ordered after the barrier

For a peer that negotiated the barrier, the service SHALL stop and join the superseded scan worker for that volume, then write `ScanAttemptStarted` for that volume, and only then register and start the replacement worker. Replacement batches and terminals SHALL be written after that barrier. Old worker output SHALL NOT be written after it. A failed barrier write SHALL tear down the connection and SHALL NOT start the replacement worker.

#### Scenario: The service orders the barrier between the old and new workers

- **WHEN** a new scan request supersedes a live scan for the same connection-scoped volume id on a 2.3 connection
- **THEN** the service SHALL join the old worker before writing the barrier
- **AND** SHALL start the replacement worker only after that write succeeds

#### Scenario: A failed barrier write starts nothing

- **WHEN** the barrier write fails
- **THEN** the service SHALL NOT start the replacement worker
- **AND** SHALL fail the connection instead of emitting unordered replacement output

### Requirement: Engine authority follows the barrier

Before the matching barrier arrives, the engine SHALL reject scan batches, scan completion, and scan failure as authority for that provisional replacement. After the matching barrier arrives, replacement batches SHALL be accepted and the replacement's own completion or failure SHALL be authoritative. A replacement failure after the barrier SHALL enter the single-owner retry path. A duplicate barrier, an unsolicited barrier, a barrier for an unknown volume, or a barrier from a connection that is no longer current SHALL NOT grant or revoke authority. Disconnect before the barrier, and disconnect immediately after it, SHALL leave no false completion and SHALL NOT leave the replacement permanently in flight without an owner for owed retry work.

#### Scenario: Frames before the barrier have no authority

- **WHEN** a provisional replacement is waiting for its barrier and a batch, completion, or failure arrives first
- **THEN** the engine SHALL NOT apply that frame as replacement authority
- **AND** SHALL keep the coverage hole until a barrier that belongs to the current attempt arrives

#### Scenario: Frames after the barrier are the replacement

- **WHEN** the matching barrier has been accepted and the replacement then sends batches and a completion or failure
- **THEN** the engine SHALL accept those batches
- **AND** SHALL treat that terminal as the replacement's authority
- **AND** a failure terminal SHALL arm exactly one retry owner

#### Scenario: Duplicate, unsolicited, unknown, and stale barriers do nothing

- **WHEN** a second barrier arrives for an attempt that already accepted one, a barrier arrives with no provisional replacement, the volume id is unknown, or the barrier belongs to a connection that was replaced
- **THEN** the engine SHALL NOT change current scan authority
- **AND** SHALL NOT clear a coverage hole or mark a volume complete from that barrier

#### Scenario: Disconnect around the barrier stays owned

- **WHEN** the connection drops before the barrier arrives, or drops immediately after the barrier is accepted and before a terminal
- **THEN** the engine SHALL NOT mark the volume complete
- **AND** SHALL NOT leave owed retry work without an owner
- **AND** SHALL NOT accept a later terminal from the lost connection as authority

#### Scenario: Replacement start failure after a written barrier

- **WHEN** the barrier was written and the replacement worker then fails to start, or the client start request fails after a possible partial write
- **THEN** the service SHALL NOT leave an unacknowledged worker running without a connection teardown
- **AND** the engine SHALL NOT treat the partial or missing start as success
- **AND** owed coverage SHALL remain retryable under a new connection-scoped id

### Requirement: A stale transport generation cannot move scan authority

A barrier, batch, or terminal from an unauthenticated connection, a stale authentication generation, or a replaced transport generation SHALL NOT change the current connection's scan authority.

#### Scenario: Replaced generation is ignored

- **WHEN** a barrier is delivered for a transport generation that is not the authenticated generation currently owning the volume session
- **THEN** the engine SHALL ignore it for scan authority
- **AND** the current generation's provisional or admitted attempt SHALL remain unchanged
