## ADDED Requirements

### Requirement: Retry intent has exactly one owner

When a scan failure, a non-authoritative completion, or a lost connection creates retry intent for a durable volume, that intent SHALL have exactly one owner until it is issued on a live authenticated session, transferred to the next owner, or explicitly cleared by engine shutdown. The owners are the in-flight attempt, the connection-scoped retry queue, the scheduler's in-hand dispatch, and the durable reconnect floor. Removing an owner SHALL install the next owner before any other connection-state or scheduler observation can see the intent as absent. A disconnect SHALL NOT clear the session while the intent belongs to none of those owners. An already-indexed volume SHALL NOT fall back to only the ordinary reconciliation interval because a raced disconnect dropped the five-second retry.

#### Scenario: Scan failure races disconnect

- **WHEN** a reconciliation scan is active, the failure terminal clears the in-flight attempt, and the connection disconnects before the retry is entered into its next owner
- **THEN** the retry intent SHALL still have exactly one owner after both sides resume
- **AND** for an already-indexed volume that owner SHALL be the durable reconnect floor or the replacement connection's single retry slot
- **AND** the engine SHALL NOT wait only for the ordinary reconciliation interval to recover the coverage gap

#### Scenario: Scheduler dispatch races disconnect

- **WHEN** a due retry leaves the connection-scoped queue and the connection disconnects before that retry is adopted by a new in-flight attempt or by the durable reconnect floor
- **THEN** the in-hand retry SHALL remain owned for the durable volume
- **AND** SHALL NOT disappear because the queue no longer contains it and the session no longer shows an in-flight scan

#### Scenario: A late arm cannot land on a dead connection id

- **WHEN** disconnect has already cleared the connection-scoped session and a failure path then tries to arm a retry for that connection's volume id
- **THEN** the engine SHALL NOT leave the intent only under the discarded id
- **AND** SHALL place it on the durable floor or drop it only because shutdown has already cleared retry owners

### Requirement: Retry edges stay single-owner and throttled

The engine SHALL keep one retry intent per durable volume. Each re-arm SHALL be due no earlier than five seconds after the event that re-armed it. The engine SHALL NOT tight-loop reconnects, SHALL NOT send a request that uses a volume id from a lost connection, and SHALL NOT run two overlapping reconciliation passes for the same volume. Pause, disable, and disappearance SHALL NOT issue a scan. Shutdown SHALL clear pending retry owners and SHALL NOT issue a scan afterward. A failed or interrupted admission SHALL NOT create a second intent.

#### Scenario: Admission fails or disconnects during submission

- **WHEN** a retry request fails to send, the connection disconnects during submission, or the connection disconnects immediately after the retry is admitted
- **THEN** the durable volume SHALL retain exactly one retry owner
- **AND** a still-live authenticated session SHALL re-arm only that slot at the five-second floor
- **AND** a lost connection SHALL transfer that slot to the durable floor rather than dropping it

#### Scenario: Pause, disable, and disappearance do not issue or duplicate

- **WHEN** a retry is due while the volume is paused or disabled, or the volume disappears from enumeration before the retry is transferred
- **THEN** the engine SHALL NOT issue a scan for it
- **AND** SHALL NOT send the discarded connection-scoped id
- **AND** SHALL keep at most one non-issuing owner for a volume that can return
- **AND** SHALL NOT delete live index rows because the retry was not issued

#### Scenario: Several volumes and repeated failure stay bounded

- **WHEN** two volumes each have a retry and one volume's reconciliation fails again after its retry is issued
- **THEN** each volume SHALL have at most one outstanding retry
- **AND** the failed volume's next due time SHALL be no earlier than five seconds after that failure

#### Scenario: Reconnect uses a fresh id and preserves kind

- **WHEN** the replacement connection enumerates the same durable volume under a new id, including after reauthentication failure prevented an earlier transfer and a later authenticated session succeeds
- **THEN** the engine SHALL attach the existing intent to that new id
- **AND** SHALL preserve whether the owed work is an initial scan or a reconciliation
- **AND** SHALL NOT reuse the lost connection's id

#### Scenario: Shutdown drops the queue without a later issue

- **WHEN** the engine shuts down while a retry is queued, in-hand, or held on the durable floor
- **THEN** the engine SHALL clear those owners
- **AND** SHALL NOT issue a scan after shutdown

#### Scenario: Already-indexed and initial recovery both keep the floor

- **WHEN** the owed work is reconciliation of an already-indexed volume, or recovery of an initial scan that never completed
- **THEN** the engine SHALL retry that work through the single-owner path
- **AND** SHALL NOT mark the volume complete or finalize a reconciliation pass from the failed attempt

### Requirement: Coverage-hole replacement and raced retry recover once

The engine SHALL recover from one sequence that has an already-indexed volume, an active reconciliation, a pause, a dropped batch that creates a coverage hole, a resume that requests replacement, an old worker that emits no terminal, a scan-start barrier, a replacement failure, a failure-terminal race with disconnect, a reconnect under a fresh volume id, a transferred retry, and a later authoritative replacement completion. Until that sequence passes, the retry-ownership gap SHALL be treated as open.

#### Scenario: Combined barrier, failure, disconnect, and completion

- **WHEN** that sequence occurs
- **THEN** the engine SHALL NOT delete a live row from the incomplete reconciliation
- **AND** SHALL NOT report false completion
- **AND** SHALL NOT lose or duplicate the retry
- **AND** SHALL NOT reuse a stale volume id
- **AND** SHALL preserve the five-second throttle
- **AND** the authoritative replacement SHALL complete exactly once
