## MODIFIED Requirements

### Requirement: Batched Transactional Writes Under WAL

Ingestion of scanned or streamed records into the durable store SHALL be
performed as explicit, multi-record transactions (batched commits), not one
transaction per individual record, and SHALL permit concurrent readers on
independent read sessions/connections to proceed without being blocked by an
in-progress ingestion transaction. A `Store` instance itself is
connection-owned and is not thereby declared safe for concurrent calls from
multiple threads. The supported concurrency contract is one authoritative
writer with a bounded set of independent readers; this requirement does not
establish simultaneous multi-writer ownership. Read sessions SHALL not perform
migrations, writes, vacuum, or checkpoint ownership.

#### Scenario: A batch of streamed records commits as one transaction

- **WHEN** the engine receives a batch of records from an active scan or
  journal stream
- **THEN** the engine SHALL write the entire batch within a single database
  transaction rather than committing each record individually

#### Scenario: An independent read session overlaps a write

- **WHEN** a reconciliation sweep, indexed search, aggregate, or diagnostic
  read is active on an independent session
- **THEN** an ingestion write transaction committing during that time SHALL
  NOT be blocked by, and SHALL NOT block, that read

#### Scenario: A concurrent read is not blocked by an in-progress write transaction

- **WHEN** a reconciliation sweep or diagnostic read is in progress against
  the durable store on an independent SQLite connection
- **THEN** an ingestion write transaction committing during that time SHALL
  NOT be blocked by, and SHALL NOT block, that concurrent read

#### Scenario: A held reader keeps a coherent pre-commit snapshot

- **WHEN** a read statement has begun on one independent connection and remains
  active while the authoritative writer commits a batch on another connection
  to the same WAL database
- **THEN** the already-started read SHALL complete against one coherent
  pre-commit snapshot and SHALL NOT observe a torn mixture of pre- and
  post-commit rows

#### Scenario: A fresh read observes the completed commit

- **WHEN** the held pre-commit read finishes after a concurrent writer has
  successfully committed
- **THEN** a new read statement on that reader/session SHALL observe the
  committed state without requiring the engine database to be recreated

#### Scenario: Passive checkpoint does not invalidate an active reader

- **WHEN** a passive WAL checkpoint runs while an independent read snapshot is
  active
- **THEN** the checkpoint SHALL return in bounded time, SHALL NOT invalidate or
  corrupt the active reader, and MAY leave frames uncheckpointed while that
  snapshot pins them

#### Scenario: Supported overlap does not imply multi-writer support

- **WHEN** the one-writer/bounded-independent-reader contract is exercised
- **THEN** implementations and tests SHALL NOT infer that a single `Store`
  object is thread-safe or that multiple independent writers may concurrently
  own the same database

### Requirement: Compact In-Memory Projection

`FastFilesEngine` SHALL maintain an in-memory projection of the index in which
each entry is a fixed-size record referencing its parent by
`FileReferenceNumber` (or an equivalent dense internal reference) and its name
by an interned/deduplicated string id, rather than by a stored full path
string. This projection SHALL remain a bounded working set: when its estimated
memory exceeds the configured maximum, subtree eviction SHALL reduce it,
evicted data SHALL remain durable in the store, and the eviction/reload policy
SHALL be observable through testable diagnostics. No read path (search, browse,
aggregate, snapshot export) MAY reconstruct the complete durable index in RAM
merely to answer a query. Search SHALL instead capture bounded stable hot
chunks and use bounded result accumulation, with a defined generation check
before publication.

#### Scenario: Forced eviction keeps both projection and query memory bounded

- **Given** a store with more entries than a deliberately small projection
  budget
- **When** ingestion and broad searches run repeatedly against that budget
- **Then** the projection remains within its configured ceiling and transient
  search memory is limited to the configured chunk and bounded top-N results

#### Scenario: Sibling entries with long shared ancestor paths do not duplicate that path in memory

- **WHEN** the projection holds many entries under a common, deeply-nested
  ancestor directory
- **THEN** the shared ancestor path SHALL NOT be stored redundantly per entry;
  each entry SHALL reference its parent rather than storing a materialized path

#### Scenario: Repeated file/directory names across the tree share one interned string

- **WHEN** the same name occurs under many different parents across the indexed
  volume
- **THEN** the projection SHALL store that name once in its interned string pool
  and reference it by id from every entry with that name, rather than storing
  the string once per entry

#### Scenario: A full path is reconstructed only when needed

- **WHEN** a UI client or engine-internal consumer needs an entry's full path
  for display or navigation
- **THEN** the engine SHALL reconstruct it by walking parent references and
  concatenating interned names on demand, rather than reading a precomputed
  path field

#### Scenario: Search does not reload a cold volume

- **Given** a volume whose projection residency has been evicted
- **When** an indexed search or aggregate reads that volume
- **Then** durable rows answer the request without reconstructing the complete
  volume in RAM

#### Scenario: Diagnostics report bounded read policy

- **Given** any projection and read-plane state
- **When** diagnostics are requested
- **THEN** they report projection memory, bounded chunk/reader configuration,
  active/peak durable readers, generation restarts, and query failures without
  exposing filenames or paths
