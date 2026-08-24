## Purpose

Defines independent, read-only SQLite sessions for durable indexed reads so
long cold queries can hold WAL snapshots without sharing the writer `Store`
object or blocking ingestion.

## ADDED Requirements

### Requirement: Durable Readers Own Independent Read Connections

Every long-lived durable indexed read SHALL use an independent SQLite
connection/session opened against the same database file. The authoritative
writer `Store` object SHALL remain connection-owned and SHALL NOT be called
concurrently from multiple threads. A read session SHALL be read-only,
query-only, non-migrating, non-writing, and shall not own vacuum or checkpoint
maintenance.

#### Scenario: Cold search holds a reader while the writer commits

- **WHEN** a cold search holds an active read statement or transaction on its
  independent session
- **THEN** the authoritative writer SHALL commit a real ingestion batch on its
  own `Store` connection without waiting for the search to finish

#### Scenario: Reader session cannot become a second writer

- **WHEN** a read session is used for search, path lookup, or aggregation
- **THEN** it SHALL reject or lack any write/migration operation and SHALL not
  broaden the one-writer product contract

### Requirement: Durable Read Snapshots Are Coherent and Refreshable

A read session used by one query SHALL pin one SQLite WAL snapshot for that
query. All rows, point lookups, and aggregates used by that query SHALL come
from that snapshot. A later query SHALL be able to open a fresh snapshot and
observe a committed writer change without recreating the engine database.

#### Scenario: Held snapshot sees pre-commit state

- **WHEN** a durable reader begins before a writer commits an insert/update/
  remove batch and remains active while that batch commits
- **THEN** the held reader SHALL see one coherent pre-commit state, with no
  torn mixture of old and new rows

#### Scenario: Fresh reader sees post-commit state

- **WHEN** the writer commit completes and the prior reader is released
- **THEN** a fresh read statement/session SHALL observe the committed rows,
  updates, and removals

#### Scenario: Passive maintenance overlaps a reader

- **WHEN** passive WAL maintenance runs while a read snapshot is active
- **THEN** maintenance SHALL return in bounded time and SHALL not invalidate or
  corrupt the reader, even if SQLite leaves frames in the WAL

### Requirement: Durable Streams Are Cancellable and Fail Closed

Read-session row streams and point-lookups SHALL accept cooperative
cancellation and SHALL release statements/transactions deterministically. A
stream or session failure SHALL be distinguishable from a normal visitor stop;
the engine SHALL never present rows from an aborted stream as a complete index
result.

#### Scenario: Cancellation interrupts a cold stream

- **WHEN** a cancellation token is signalled while a row statement is active
- **THEN** the visitor SHALL stop promptly, the statement SHALL finalize, and
  the session SHALL be reusable or closed without a leaked transaction

#### Scenario: Mid-volume stream failure

- **WHEN** a read fault occurs after only part of a volume has streamed
- **THEN** the caller SHALL receive an unavailable/failure outcome rather than
  a partial successful result

### Requirement: Reader Resources Have Deterministic Ownership and Bounds

The read plane SHALL bound the number of simultaneously open read sessions and
shall close every session on completion, cancellation, generation restart,
fault, client disconnect, and engine shutdown. Reader acquisition SHALL be
observable and SHALL NOT use an unbounded cache or retry loop.

#### Scenario: Generation restart releases the old reader

- **WHEN** a query detects a stale generation and restarts
- **THEN** its old read session/statement SHALL be closed before the next
  bounded reader is acquired

#### Scenario: Shutdown releases reader state

- **WHEN** the engine closes while a read session is active
- **THEN** the session SHALL be cancelled/closed deterministically and the
  database SHALL remain reopenable and integrity-checkable

### Requirement: Read-Session Faults Preserve Writer Health

An inability to open a reader, begin a snapshot, step a statement, or resolve a
durable point lookup SHALL fail the affected read closed without corrupting or
closing the authoritative writer connection. Fault injection SHALL cover
reader open, busy/locked/read failure, stream abort, cancellation during an
active statement, volume forget, and database close/shutdown.

#### Scenario: Reader open failure

- **WHEN** a reader cannot open or begin its read snapshot
- **THEN** the query reports unavailable, no reader is leaked, and a subsequent
  writer batch remains functional

#### Scenario: Database closes during controlled shutdown

- **WHEN** the writer/database closes while a controlled read is being
  cancelled
- **THEN** no crash or deadlock occurs, the read reports cancelled/unavailable,
  and a later fresh database open is not left busy or corrupt
