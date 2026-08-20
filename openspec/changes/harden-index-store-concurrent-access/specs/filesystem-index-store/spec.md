# filesystem-index-store delta — harden-index-store-concurrent-access

## MODIFIED Requirements

### Requirement: Batched Transactional Writes Under WAL
Ingestion of scanned or streamed records into the durable store SHALL be performed as explicit, multi-record transactions (batched commits), not one transaction per individual record, and SHALL permit concurrent readers on independent SQLite connections (reconciliation sweeps, diagnostics, or equivalent read-only consumers) to proceed without being blocked by an in-progress ingestion transaction. A `Store` instance itself is connection-owned and is not thereby declared safe for concurrent calls from multiple threads. The supported concurrency contract is one authoritative writer with independent readers; this requirement does not establish simultaneous multi-writer ownership.

#### Scenario: A batch of streamed records commits as one transaction
- **WHEN** the engine receives a batch of records from an active scan or journal stream
- **THEN** the engine SHALL write the entire batch within a single database transaction rather than committing each record individually

#### Scenario: A concurrent read is not blocked by an in-progress write transaction
- **WHEN** a reconciliation sweep or diagnostic read is in progress against the durable store on an independent SQLite connection
- **THEN** an ingestion write transaction committing during that time SHALL NOT be blocked by, and SHALL NOT block, that concurrent read

#### Scenario: A held reader keeps a coherent pre-commit snapshot
- **WHEN** a read statement has begun on one connection and remains active while the authoritative writer commits a batch on another connection to the same WAL database
- **THEN** the already-started read SHALL complete against one coherent pre-commit snapshot and SHALL NOT observe a torn mixture of pre- and post-commit rows

#### Scenario: A fresh read observes the completed commit
- **WHEN** the held pre-commit read finishes after a concurrent writer has successfully committed
- **THEN** a new read statement on that reader connection SHALL observe the committed state without requiring the process or Store connection to be recreated

#### Scenario: Passive checkpoint does not invalidate an active reader
- **WHEN** a passive WAL checkpoint runs while an independent read snapshot is active
- **THEN** the checkpoint SHALL return in bounded time, SHALL NOT invalidate or corrupt the active reader, and MAY leave frames uncheckpointed while that snapshot pins them

#### Scenario: Supported overlap does not imply multi-writer support
- **WHEN** the one-writer/many-reader concurrency contract is exercised
- **THEN** implementations and tests SHALL NOT infer that a single `Store` object is thread-safe or that multiple independent writers may concurrently own the same database
