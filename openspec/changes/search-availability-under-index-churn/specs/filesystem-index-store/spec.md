## ADDED Requirements

### Requirement: Operational Volume Metadata Is Not Query-Visible State

The durable store SHALL distinguish volume metadata that records scan/journal
progress or maintenance bookkeeping from entries and volume configuration that
can change an indexed query answer. Updating only a scan cursor, journal
position, scan-completion marker, last-reconciliation timestamp, passive
checkpoint, planner statistics, or maintenance timestamp SHALL preserve the
one-writer contract without requiring a query-visible invalidation.

#### Scenario: Cursor and journal progress overlap a query

- **WHEN** the writer persists scan-cursor or journal-position progress while a
  query is active
- **THEN** the query's captured searchable state SHALL remain valid and the
  writer SHALL continue to commit progress normally

#### Scenario: Reconciliation bookkeeping overlaps a query

- **WHEN** a reconciliation pass updates only its progress/timestamp markers
  and performs no entry or searchable-volume mutation
- **THEN** the query SHALL not restart solely because those markers were
  persisted

#### Scenario: Actual reconciliation removal remains visible

- **WHEN** reconciliation removes an entry or changes searchable inclusion
  state in addition to updating bookkeeping
- **THEN** the affected volume's query-visible state SHALL invalidate active
  dependent queries and the durable store SHALL remain authoritative

