## ADDED Requirements

### Requirement: A stale scan terminal cannot authorize replacement state

The engine SHALL treat scan completion and scan failure as authority only for the attempt that is still in flight. A terminal from an attempt the engine has replaced or retired SHALL NOT mark the volume scan complete, SHALL NOT finalize a reconciliation pass, and SHALL NOT clear a recorded coverage hole. Resuming indexing SHALL NOT make an older terminal authoritative. A completion or failure message that carries only the connection-scoped volume id SHALL NOT become distinguishable from another attempt merely because the engine incremented a local counter. Disconnect SHALL NOT restore authority to a terminal that names a volume id from the connection that was lost.

#### Scenario: Resume clears nothing for an older completion of an incomplete scan

- **WHEN** an initial scan has applied a batch, indexing is paused, a later batch is dropped so coverage has a hole, indexing is resumed so replacement work is issued, and the original attempt's completion is delivered before the replacement finishes
- **THEN** the engine SHALL NOT mark the volume scan complete
- **AND** SHALL keep the coverage hole recorded against that attempt

#### Scenario: A failed replacement does not inherit a stale success

- **WHEN** an older completion was not authoritative and the replacement attempt then fails or the connection drops before the replacement's own terminal
- **THEN** the engine SHALL NOT leave the volume scan complete on the strength of the older completion
- **AND** SHALL NOT finalize a reconciliation pass from either attempt

#### Scenario: Ordinary pause, resume, and success still complete

- **WHEN** indexing is paused and resumed and the completion that arrives is ordered after the replacement attempt, and every batch of that attempt was applied
- **THEN** the engine SHALL mark the volume scan complete
- **AND** SHALL resume from the last durably applied position rather than discarding the volume's index

#### Scenario: Disconnect cannot restore a stale terminal

- **WHEN** the privileged connection leaves the active state and a completion or failure for the previous connection's volume id is delivered afterward
- **THEN** the engine SHALL NOT mark the volume scan complete from that terminal
- **AND** SHALL NOT finalize a reconciliation pass from that terminal

#### Scenario: A peer that cannot represent scan failure still gets no false completion

- **WHEN** a scan does not finish and the negotiated peer has no failure terminal the engine can trust
- **THEN** the engine SHALL NOT mark the volume scan complete because the peer was not told of the failure

### Requirement: Incomplete coverage stays incomplete and is retried without deleting valid rows

An initial scan that dropped a batch or failed to apply a batch SHALL remain not scan-complete. The engine SHALL retry the missing coverage from the last durably applied position. An incomplete reconciliation SHALL NOT delete records that the incomplete pass did not observe. A replacement attempt SHALL NOT be granted a clean completion slate while an unresolved older terminal can still refer to the same connection-scoped volume id.

#### Scenario: A paused coverage hole remains incomplete

- **WHEN** a batch is dropped while indexing is paused during an initial scan that had not completed
- **THEN** the volume SHALL stay not scan-complete after that attempt ends without a replacement that actually covered the hole

#### Scenario: Missing records are retried from the frozen position

- **WHEN** the engine retries an incomplete initial scan
- **THEN** the retry SHALL start from the last durably applied cursor
- **AND** the volume SHALL become scan-complete only after an attempt that applied the retried coverage finishes authoritatively

#### Scenario: Incomplete reconciliation does not delete a live record

- **WHEN** a reconciliation pass has not observed a record that is already in the durable index and that pass ends incomplete, paused, failed, or disconnected
- **THEN** the engine SHALL NOT remove that record
- **AND** SHALL abort the pass instead of finalizing it

### Requirement: Interrupted reconciliation retries on the new volume id at the five-second floor

When reconciliation of an already-indexed volume is in progress and the connection is lost before an authoritative terminal, the engine SHALL keep that retry intent after the old connection-scoped volume id is gone. When a later connection enumerates the same durable volume under a new volume id, the engine SHALL attach exactly one retry to that new id. The retry SHALL honor a five-second floor measured from the interruption. The engine SHALL wake that retry without using the ordinary multi-minute scheduler poll or the multi-hour reconciliation interval as the only wake. The engine SHALL NOT send a scan request that uses a volume id from the lost connection. The engine SHALL NOT run two overlapping reconciliation passes for the same volume. Repeated failure SHALL remain throttled to that floor, and the pending retry state SHALL stay bounded by one outstanding retry per volume.

#### Scenario: Reconnect queues the five-second retry instead of the ordinary poll

- **WHEN** an already-indexed volume loses its connection during reconciliation and the replacement connection enumerates that volume with a new volume id before the five-second floor elapses
- **THEN** the engine SHALL queue one reconciliation retry on the new id due at that floor
- **AND** SHALL NOT leave the retry waiting only for the ordinary scheduler poll or the ordinary reconciliation interval

#### Scenario: An already-expired floor does not tight-loop or wait for the long interval

- **WHEN** the same durable volume is recognized only after the five-second floor has already elapsed
- **THEN** the engine SHALL issue one throttled reconciliation retry through the retry queue
- **AND** SHALL NOT immediately reconnect and rescan in a loop
- **AND** SHALL NOT defer that retry to the ordinary multi-hour interval

#### Scenario: A stale connection-scoped id is not used

- **WHEN** a retry becomes due after the volume id that owned the interrupted pass has been discarded
- **THEN** the engine SHALL NOT send a scan or journal request with that discarded id

#### Scenario: One retry slot survives repeated failure and several volumes

- **WHEN** two volumes each have an interrupted reconciliation and one of them fails again after the retry is issued
- **THEN** each volume SHALL have at most one outstanding retry
- **AND** the failed volume's next due time SHALL be no earlier than five seconds after that failure

#### Scenario: Pause, disable, and disappearance do not issue a scan

- **WHEN** the durable volume is paused, disabled, or absent from the replacement enumeration while a retry intent exists
- **THEN** the engine SHALL NOT issue a scan for it
- **AND** SHALL NOT drop a still-valid paused intent solely because the connection was replaced

#### Scenario: Admission failure, reauthentication failure, and shutdown stay bounded

- **WHEN** a retry send fails, the replacement connection is not authenticated, the connection drops during admission, or the engine shuts down while a retry is pending
- **THEN** the engine SHALL NOT create an additional overlapping pass
- **AND** SHALL NOT issue a request after shutdown
- **AND** a failed send on a still-live authenticated session SHALL re-arm only the single throttled slot

### Requirement: Pause, disconnect, and reconnect recover without stale authority

The engine SHALL recover from one sequence that pauses indexing, resumes it, interrupts an in-flight reconciliation by disconnecting, reconnects with a new volume id, delivers a terminal from the retired attempt, and then completes a later authoritative attempt. Until that sequence preserves the rules in this spec, the pause/resume authority gap and the interrupted-reconciliation gap SHALL both be treated as open.

#### Scenario: Combined pause, disconnect, and stale terminal still recover

- **WHEN** a volume is paused during scan or reconciliation work, a batch is dropped, the connection is lost, a new connection assigns a new volume id, a terminal for the retired id or retired attempt arrives, and a later attempt then runs to completion
- **THEN** the stale terminal SHALL NOT mark the volume complete and SHALL NOT delete valid records
- **AND** the interrupted work SHALL be retried under the five-second rule
- **AND** the later authoritative completion SHALL be able to mark the volume scan complete
