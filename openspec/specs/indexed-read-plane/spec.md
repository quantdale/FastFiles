# indexed-read-plane Specification

## Purpose

The bounded indexed-query read plane that remains responsive while the engine
ingests, publishes, reconciles, serves aggregates, or shuts down.

## Requirements

### Requirement: Indexed Read Work Does Not Hold the Mutation Critical Section

Indexed search SHALL perform long-running enumeration, durable streaming,
matching, path reconstruction, sorting, and response preparation outside the
engine's mutation/lifecycle critical section. Any live projection state held by
the query SHALL be captured in bounded stable chunks, and the critical section
SHALL be reacquired only for bounded capture or generation validation.

#### Scenario: Broad search overlaps ingestion

- **WHEN** a broad indexed search is deliberately held in matching or sorting
- **THEN** a real MFT or USN ingestion batch SHALL commit and apply without
  waiting for the search to finish

#### Scenario: Search overlaps an aggregate request

- **WHEN** a long indexed search is active
- **THEN** a folder aggregate SHALL complete from a valid projection or
  independent durable read snapshot without waiting for the full search

#### Scenario: Search overlaps snapshot or navigation publication

- **WHEN** a long indexed search is active and an unrelated directory
  publication is requested
- **THEN** the publication path SHALL make progress independently of the
  search's matcher and sorter

### Requirement: Indexed Query Results Have an Explicit Stable Generation

An indexed query SHALL execute against one coherent query-visible state. The
engine SHALL capture a named query-visible epoch, the relevant volume/configuration
dependencies, and a durable read context before long-running work. It SHALL
discard work that observes a relevant dependency change and deterministically
restart from a newly captured context up to a bounded hybrid restart limit. If
relevant churn exhausts that limit, the query SHALL switch to one independent
durable SQLite snapshot before reporting `Unavailable`, subject to bounded
configuration retries. It SHALL never publish a mixed-generation or partial
result.

#### Scenario: Mutation during a query

- **WHEN** an insert, rename, move, searchable metadata update, delete,
  reconciliation removal, relevant volume availability/configuration change,
  or captured residency change occurs during an indexed query
- **THEN** the query SHALL either complete from one stable context, restart,
  switch to one durable snapshot, or cancel before publishing a result

#### Scenario: Bookkeeping during a query

- **WHEN** only a scan cursor, journal position, scan-completion marker,
  reconciliation timestamp, checkpoint, planner-statistics, or maintenance
  state changes during an indexed query
- **THEN** the query SHALL not restart solely for that bookkeeping change

#### Scenario: A query is superseded during a restart

- **WHEN** cancellation is signalled while a query is validating, restarting,
  or executing durable fallback work
- **THEN** the query SHALL stop and report `Cancelled` without publishing any
  result from an earlier context

#### Scenario: Restart budget is exhausted

- **WHEN** repeated relevant mutations prevent a query from obtaining a stable
  hybrid context within the configured bounded restart budget
- **THEN** the query SHALL attempt one bounded durable-snapshot execution and
  SHALL report `Unavailable` only when that snapshot or its configuration/
  lifecycle validation cannot complete safely; it SHALL NOT report a partial
  or fabricated complete result

### Requirement: Indexed Read Memory and Work Fan-Out Are Bounded

The read plane SHALL use a fixed maximum number of active query executions, a
fixed maximum queued work count, a fixed hot-row capture chunk bound, and the
existing indexed result cap. Queued work SHALL be FIFO across clients, and a
cancelled queued request SHALL be removed or skipped without consuming a
reader or worker indefinitely. The design SHALL NOT create an owning O(N)
candidate vector, an unbounded reader/session cache, or a thread per indexed
row/result.

#### Scenario: Reader capacity is exhausted

- **WHEN** the active query/read capacity is full
- **THEN** additional requests SHALL wait only in the bounded FIFO queue or
  receive an explicit fail-closed unavailable response when the queue is full

#### Scenario: Queued cancellation releases capacity

- **WHEN** a queued request is cancelled before execution
- **THEN** it SHALL complete as cancelled, allocate no durable reader, and the
  next queued client SHALL be eligible to run

#### Scenario: Broad result memory stays bounded

- **WHEN** a broad query scans an index larger than the projection budget
- **THEN** transient hot/cold candidate memory SHALL remain limited to the
  configured chunk plus the bounded top-N result set

### Requirement: Indexed Search Cancellation and Client Isolation Are Preserved

The newest indexed search supersedes only the older search for the same UI
client. Cancellation SHALL be connection-scoped, checked during hot capture,
cold streaming, path reconstruction, matching, top-N processing, generation
validation, queue waiting, and response publication. A cancelled request SHALL
NOT write a result after cancellation, and one client SHALL NOT cancel another.

#### Scenario: One client supersedes itself

- **WHEN** client A starts request 2 while its request 1 is active or queued
- **THEN** request 1 is cancelled and only request 2 may publish a result for A

#### Scenario: One client cannot cancel another

- **WHEN** client A sends cancellation for an identifier owned by client B
- **THEN** B's request SHALL continue unchanged and A's cancellation SHALL have
  no effect on B

#### Scenario: Cancellation during durable work

- **WHEN** cancellation occurs while cold rows or parent paths are being read
- **THEN** the SQLite statement/session SHALL be released promptly and the
  response SHALL be `Cancelled`

### Requirement: Indexed Read-Plane Lifecycle and Faults Fail Closed

Stopping the engine, stopping `UiServer`, disconnecting a client, closing the
durable database, exhausting the reader queue, or encountering an unrecoverable
read/generation/serialization fault SHALL cancel or fail affected work
deterministically. No worker, statement, transaction, callback, or reader
resource SHALL outlive the owning lifecycle, and no partial result SHALL be
reported as complete.

#### Scenario: Engine shutdown with active searches

- **WHEN** engine or UI-server shutdown begins while one or more searches are
  active or queued
- **THEN** cancellation/drain completes within the bounded shutdown contract,
  no post-destruction callback runs, and all search resources are reusable or
  closed

#### Scenario: Read failure after partial streaming

- **WHEN** an independent read connection, transaction, statement, or cold
  stream fails after rows have already been examined
- **THEN** the query reports `Unavailable` (unless cancellation won first),
  does not publish the partial rows, and subsequent writes and reads remain
  usable

#### Scenario: Response exceeds its normal frame budget

- **WHEN** response serialization cannot produce a valid bounded response
- **THEN** the query fails closed without writing an oversized or partial frame

### Requirement: Read-Plane Diagnostics Are Bounded and Testable

The engine SHALL expose structured diagnostics for active and queued searches,
active and peak durable readers, reader acquisition wait, query-visible and
dependency-set epochs, relevant invalidations, metadata-only invalidations
avoided, hybrid restart count, durable fallback attempts and success/failure,
fallback rows streamed, hot/cold rows examined, cancellation count, search
duration, critical-section wait/hold observations, and reader/query failures.
Diagnostics SHALL be bounded, SHALL NOT include arbitrary personal filenames or
paths, and SHALL remain optional/noisy-output-free in the normal product path.

#### Scenario: A contention run reports read-plane state

- **WHEN** a deterministic contention test runs a long cold query beside
  ingestion
- **THEN** diagnostics identify the query context, relevant invalidations,
  avoided metadata churn, active/peak reader use, fallback rows, rows by
  hot/cold source, lock wait/hold observations, and cancellation or restart
  outcomes without logging user paths
