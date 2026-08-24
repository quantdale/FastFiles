## MODIFIED Requirements

### Requirement: Indexed Query Results Have an Explicit Stable Generation

An indexed query SHALL execute against one coherent query-visible state. The
engine SHALL capture the explicit query-visible epoch and scope dependencies
before long-running work, discard work that observes a relevant dependency
change, and deterministically restart from a newly captured context up to a
bounded restart limit. If relevant churn exhausts that hybrid restart limit,
the query SHALL attempt the bounded durable-snapshot fallback defined by
`search-availability-under-index-churn`. If the fallback cannot establish a
safe snapshot, the query SHALL fail closed as `Unavailable`; it SHALL never
publish a mixed-generation or partial result.

#### Scenario: Mutation during a query

- **WHEN** an insert, rename, move, searchable metadata update, delete,
  reconciliation removal, volume visibility/configuration change, or relevant
  residency change occurs during an indexed query
- **THEN** the query SHALL either complete from one stable context, restart,
  switch to one durable snapshot, or cancel before publishing a result

#### Scenario: Bookkeeping during a query

- **WHEN** only a scan cursor, journal position, scan-completion marker,
  reconciliation timestamp, or maintenance/checkpoint state changes during an
  indexed query
- **THEN** the query SHALL not restart solely for that bookkeeping change and
  SHALL retain its existing coherent result contract

#### Scenario: A query is superseded during a restart

- **WHEN** cancellation is signalled while a query is validating, restarting,
  or executing durable fallback work
- **THEN** the query SHALL stop and report `Cancelled` without publishing any
  result from an earlier context

#### Scenario: Restart budget is exhausted

- **WHEN** relevant mutations exhaust the bounded hybrid restart budget and the
  independent durable snapshot cannot be opened or read successfully, or a
  bounded fallback cannot establish a safe configuration
- **THEN** the query SHALL report `Unavailable` with bounded diagnostics and
  SHALL NOT report a partial or fabricated complete result; if the durable
  snapshot is healthy, it SHALL be used before this outcome

### Requirement: Read-Plane Diagnostics Are Bounded and Testable

The engine SHALL expose structured diagnostics for active and queued searches,
active and peak durable readers, reader acquisition wait, query-visible epoch
and relevant dependency observations, invalidations caused, metadata-only
invalidations avoided, hybrid restart count, durable fallback attempts and
success/failure, fallback rows streamed, cancellation count, search duration,
critical-section wait/maximum-hold observations, and reader/query failures.
Diagnostics SHALL be bounded, SHALL NOT include arbitrary personal filenames or
paths, and SHALL remain optional/noisy-output-free in the normal product path.

#### Scenario: A contention run reports read-plane state

- **WHEN** a deterministic contention test runs a long query beside ingestion
  and bookkeeping writes
- **THEN** diagnostics SHALL identify the query context, relevant invalidations,
  avoided metadata churn, active/peak reader use, fallback rows, lock
  observations, and cancellation or restart outcomes without logging user
  paths
