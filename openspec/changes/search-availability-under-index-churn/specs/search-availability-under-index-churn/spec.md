## Purpose

Keeps indexed search coherent and normally available while the durable index is
being continuously updated, using explicit query-visible dependencies and a
bounded durable snapshot fallback instead of unbounded restart retries.

## ADDED Requirements

### Requirement: Query-Visible Epochs Describe Searchable State

The engine SHALL maintain an explicit query-visible epoch contract separate
from lifecycle cancellation and operational index bookkeeping. A successful
mutation SHALL advance the epoch only when it can change searchable rows,
searchable-volume visibility, inclusion/configuration, or the captured
projection residency needed by a query; a failed or no-op operation SHALL NOT
advance it.

#### Scenario: File mutations advance searchable state

- **WHEN** an MFT or USN insert, rename, move, searchable metadata update,
  delete, reconciliation removal, or forget commits successfully
- **THEN** the affected searchable volume's query-visible epoch SHALL advance
  before a later query captures its context

#### Scenario: Operational bookkeeping does not advance searchable state

- **WHEN** the engine commits only a scan cursor, journal position,
  scan-completion flag, last-reconciliation timestamp, passive checkpoint,
  `ANALYZE`, or maintenance bookkeeping update
- **THEN** no active query SHALL be invalidated solely by that write

#### Scenario: Failed or empty mutation does not create invalidation churn

- **WHEN** an ingestion or metadata operation fails, or an empty/no-op batch is
  submitted
- **THEN** the query-visible epoch SHALL remain unchanged and the writer SHALL
  retain its existing failure/transaction contract

### Requirement: Search Dependencies Are Scope-Aware

An indexed query SHALL capture the query-visible epoch for every searchable
volume that can contribute to its requested scope, plus any epoch describing a
change to that dependency set. A mutation to a volume outside that dependency
set SHALL NOT restart the query; an all-volume query SHALL remain sensitive to
query-visible mutations on every included searchable volume.

#### Scenario: Narrow scope ignores unrelated volume ingestion

- **WHEN** a current-drive or path-scoped query is active for volume A and a
  searchable row on unrelated volume B is committed
- **THEN** the query SHALL NOT restart solely because of volume B's mutation

#### Scenario: Global scope observes every included volume

- **WHEN** an all-indexed-locations query is active and a searchable row,
  availability transition, rule change, or forget affects any included volume
- **THEN** the query SHALL validate that dependency and either restart, fall
  back, or cancel before publishing a response

#### Scenario: Scope configuration changes are fail-closed

- **WHEN** a volume is enabled, disabled, becomes unavailable, becomes
  searchable again, or has its include/exclude rules changed while a query is
  active
- **THEN** the affected dependency SHALL invalidate the captured context and
  the eventual response SHALL reflect the new searchable configuration or
  fail closed, never the old configuration mixed with new rows

### Requirement: Sustained Relevant Mutation Uses a Durable Snapshot Fallback

After the bounded hybrid hot/projection restart budget is exhausted by
relevant query-visible mutations, the engine SHALL attempt one
durable-snapshot-only execution when a safe durable snapshot can be opened. The
fallback SHALL stream rows from that one independent read snapshot through the
bounded result accumulator, perform matching, path reconstruction, sorting, and
top-N selection from that same snapshot, and publish only a complete response.

#### Scenario: Continuous relevant writes still complete coherently

- **WHEN** relevant MFT/USN writes continue long enough to prevent the bounded
  hybrid pass from stabilizing
- **THEN** the query SHALL complete from one coherent durable snapshot when
  that snapshot is available, rather than becoming `Unavailable` solely due to
  restart exhaustion

#### Scenario: Durable fallback does not hydrate the projection or block writes

- **WHEN** a durable fallback searches an evicted or changing dataset
- **THEN** it SHALL not reload evicted rows into the projection, SHALL not hold
  the pipeline mutation mutex during streaming/matching/sorting, and SHALL
  allow the writer to continue committing

#### Scenario: Fallback remains bounded

- **WHEN** the durable fallback scans more rows than the configured result cap
- **THEN** transient candidates SHALL remain bounded to the stream row/chunk
  and the existing top-N cap, with no owning full-index candidate collection,
  unbounded reader pool, or worker per row/result

### Requirement: Durable Fallback Failure Is Honest

The engine SHALL return `Unavailable` for a durable reader/session failure,
unrecoverable lifecycle/configuration state, shutdown, queue rejection where
the queue contract requires rejection, or response serialization failure. It
SHALL return `Cancelled` when cancellation or supersession wins, and SHALL
never convert a partial or failed durable stream into a completed response.

#### Scenario: Read failure after partial fallback enumeration

- **WHEN** a durable fallback fails after examining some rows
- **THEN** it SHALL discard all accumulated results, report `Unavailable` (or
  `Cancelled` if cancellation won first), and leave subsequent reads and writes
  usable

#### Scenario: Fallback cancellation releases resources

- **WHEN** cancellation occurs during durable streaming, parent-path lookup,
  matching, top-N processing, or final publication
- **THEN** the query SHALL report `Cancelled` without partial results and SHALL
  release its statement, transaction, and reader promptly

### Requirement: Query-Churn Diagnostics Are Bounded

The engine SHALL expose bounded diagnostics for query-visible epoch values,
relevant dependency observations where available, invalidations caused,
metadata-only invalidations avoided, hybrid restart count, durable fallback
attempts and successes/failures, fallback rows streamed, reader acquisition or
open failures, active/peak readers, cancellation, and critical-section wait or
maximum-hold observations. Diagnostics SHALL NOT include arbitrary filenames or
personal paths.

#### Scenario: Sustained-churn evidence explains the outcome

- **WHEN** a deterministic sustained-write query is executed
- **THEN** diagnostics SHALL distinguish metadata-only writes, relevant
  invalidations, restarts, fallback selection, fallback completion/failure,
  streamed rows, and resource cleanup without exposing user paths

