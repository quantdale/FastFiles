## ADDED Requirements

### Requirement: Indexed Search Remains Available Under Relevant Churn

When a bounded hybrid indexed query cannot stabilize because relevant
query-visible mutations continue, the engine SHALL use the
`search-availability-under-index-churn` durable fallback contract before
reporting `Unavailable`, provided a coherent durable snapshot can be opened.

#### Scenario: MFT churn does not exhaust search availability

- **WHEN** repeated MFT inserts or updates overlap a broad indexed search for
  longer than the historical restart budget
- **THEN** the search SHALL return a coherent completed result from one durable
  snapshot or an honest cancellation/read failure, and SHALL NOT return
  `Unavailable` solely because the hybrid restart budget was exhausted

#### Scenario: USN rename, move, and delete churn remains coherent

- **WHEN** repeated USN rename, move, metadata, and delete deltas overlap a
  broad indexed search
- **THEN** the response SHALL contain rows from one coherent durable snapshot
  and SHALL contain no mixed-generation or partial result

