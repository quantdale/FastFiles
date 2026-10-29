# index-engine (delta): durable cold-search candidate generation and hybrid residency

Proposal-stage acceptance invariants for `durable-cold-search-index-and-
hybrid-residency`. These requirements pin the correctness and resource
contract of whatever alternative the campaign's measurement phase selects;
they deliberately do not prescribe the implementation (FTS table, durable
trigram postings, hybrid residency, or another measured alternative).

## ADDED Requirements

### Requirement: Cold candidate generation shall be a conservative superset under Windows ordinal folding

Any durable candidate structure consulted by the cold path SHALL return a
conservative superset of every row the canonical matcher would accept under
`CompareStringOrdinal(..., TRUE)` host semantics, including rows whose names
contain units the host uppercase table equates with ASCII (the fold-exceptional
class) and rows containing surrogate units. Where a candidate generator cannot
prove this property structurally, the engine SHALL maintain a conservative
exception side-set equivalent to the in-memory accelerator's
fold-exceptional/surrogate handling, derived from the host table and fail-closed
on API error.

#### Scenario: Fold-exceptional name is not dropped by the candidate generator

- **WHEN** a query whose folded needle matches a fold-exceptional name (for
  example a name containing a unit the host table folds to ASCII) executes on
  the cold path with the durable candidate structure available
- **THEN** the candidate set contains that row
- **AND** the final result set is identical to the canonical scan's result set

#### Scenario: Generator failure falls back to the canonical scan

- **WHEN** the candidate structure is missing, stale, or cannot establish
  superset coverage for a query shape
- **THEN** the engine serves the query from the canonical cold scan
- **AND** results remain correct (possibly slower)

### Requirement: Cold-with-index results shall equal hot results

For every query class, results served with durable-index assistance on the
cold path SHALL be identical in identity and ordering to results served by the
hot projection path. This extends the pinned hot/cold equivalence contract to
the durable generator.

#### Scenario: Differential identity across scales

- **WHEN** the same corpus and query set are executed hot and cold-with-index
  at 200k, 1M, 2M, and 5M rows
- **THEN** result identities and orderings match exactly for all query classes

### Requirement: Index updates shall be crash-consistent with the entries table

The durable candidate structure SHALL never disagree with the entries table
after a process or machine crash. Updates SHALL be transactionally coupled to
entry mutations or carry a validity/version marker that forces rebuild-on-open
after an unclean shutdown.

#### Scenario: Kill during ingestion batch

- **WHEN** the process is terminated in the middle of an ingestion or USN
  batch that mutates both entries and the candidate structure
- **THEN** after restart the index is either fully consistent with the entries
  recorded at the last committed transaction or is rebuilt before use
- **AND** no query observes candidates for rows that do not exist

### Requirement: Schema migration and rollback shall be measured and reversible

Any schema addition SHALL be versioned and additive, with a measured migration
cost at 2M-5M rows, a documented rollback that loses no indexed data, and a
rebuild-on-open fallback when migration is interrupted.

#### Scenario: Interrupted migration

- **WHEN** migration from the previous schema version is interrupted by a crash
- **THEN** the store opens at the previous schema version with all entries
  intact and either resumes or rebuilds the candidate structure

### Requirement: The durable index shall not raise the default memory budget

Planner caches and candidate structures SHALL have documented, bounded memory
footprints. The default 500 MiB projection budget
(`Projection::kDefaultMaximumMemoryBytes`) SHALL NOT be raised by this change;
any future budget change requires its own measured safety case.

#### Scenario: Bounded caches under sustained query load

- **WHEN** sustained query traffic runs against a 5M-row store
- **THEN** documented cache bounds are not exceeded
- **AND** private bytes show no monotonic growth across query cycles

### Requirement: Planner selection shall be cost-based with canonical fallback

The engine SHALL choose between durable-index candidate generation and the
canonical scan by measured cost rules (needle eligibility, candidate cap,
structure availability), preserving the existing fallback-to-scan behavior for
ineligible query shapes.

#### Scenario: Broad query keeps the canonical scan

- **WHEN** a broad or ineligible query shape executes
- **THEN** the planner does not force candidate generation that exceeds its cap
- **AND** the canonical scan serves the query with correct results
