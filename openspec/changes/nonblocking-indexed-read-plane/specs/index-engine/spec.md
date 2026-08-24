## MODIFIED Requirements

### Requirement: Indexed Search Is Complete Under Eviction

Indexed search SHALL return results whose match identities, ordering,
tie-breaking, truncation flag, and result cap are identical whether the
matching rows are projection-resident or evicted (cold). Cold volumes SHALL be
enumerated by streaming an independent durable read session per the
`independent-index-read-sessions` capability; a searchable set containing any
cold volume SHALL use canonical matching over both sources, and the
filename-trigram accelerator SHALL be used only when every searched volume is
resident. A failed cold stream SHALL fail the query closed (`Unavailable`),
never return silent partials. Long enumeration, matching, path reconstruction,
and sorting SHALL not hold the mutation/lifecycle critical section; bounded
hot chunks and a stable generation/restart contract SHALL prevent mixed hot/cold
results.

#### Scenario: Hot/cold equivalence across query classes

- **Given** the same durable index queried twice — once fully resident, once
  with forced eviction under a small budget
- **When** selective, no-match, broad/capped, extension, structured-filter,
  Unicode, path-scoped, and incremental-typing queries run
- **Then** candidate identity lists and ordering are identical between the two
  runs for every query class

#### Scenario: Long cold search does not block ingestion

- **Given** a cold broad search is held in durable streaming or matching
- **When** the authoritative writer receives a real ingestion batch
- **Then** the batch commits and applies without waiting for the search's full
  duration

#### Scenario: Mutation produces one coherent query-visible state

- **Given** a query has captured a query-visible epoch and its relevant volume
  dependencies
- **When** a searchable row, configuration, lifecycle, or captured residency
  mutation changes that dependency
- **Then** the query SHALL restart, use a coherent durable snapshot after the
  bounded hybrid budget, or fail closed, and SHALL never publish a mixed result

#### Scenario: Operational bookkeeping does not restart indexed search

- **Given** a query is active against a retained indexed volume
- **When** only scan/journal progress, reconciliation timestamps, checkpoints,
  planner statistics, or maintenance bookkeeping changes
- **Then** the query SHALL not restart solely for that metadata write

#### Scenario: Cancellation during cold work

- **Given** a search enumerating cold store rows
- **When** cancellation is signalled mid-stream
- **Then** streaming stops promptly, the independent reader is released, and the
  response reports `Cancelled`

#### Scenario: Repeated cold searches do not thrash memory

- **Given** repeated alternating queries after eviction
- **When** each search completes
- **Then** the projection's estimated memory stays within its budget, no query
  reloads the whole volume, and transient query memory remains bounded

### Requirement: Seeded Indexed-Mode Acceptance Path

A deterministic, test-only integration suite SHALL populate the real durable
store and index pipeline with synthetic MFT/USN-equivalent records and verify
population, restart-from-SQLite, indexed navigation data, indexed and
trigram-accelerated search, canonical fallback queries, folder aggregates,
allocated-size propagation, rename/move/delete updates, reconciliation
deletion, unavailable-volume behavior, cold/evicted data, cancellation,
generation restart, bounded contention, and snapshot serialize→publish→parse
round-trips — without disabling Authenticode verification, adding an unsigned
production mode, or fabricating production MFT/USN evidence.

#### Scenario: Seeded end-to-end pipeline acceptance

- **Given** the seeded suite runs against a fresh disposable database
- **When** the full population → mutation → eviction → restart sequence and a
  concurrent read/write exercise execute
- **Then** every stage's observable state matches the deterministic expectation
  and the final state equals the reference model

#### Scenario: Unavailable indexed volumes retain queryable rows

- **WHEN** a previously indexed volume is marked unavailable while its rows are
  retained
- **THEN** indexed search SHALL continue to return retained rows until explicit
  volume configuration disables that volume
