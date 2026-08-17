# indexed-search-allocated-size — search-query-engine delta

## ADDED Requirements

### Requirement: Search candidates preserve allocated-size metadata
Projection-backed search candidates SHALL carry allocated/on-disk bytes together with an explicit known/unknown state in addition to existing logical `sizeBytes`. A known allocated value of zero SHALL remain distinguishable from unknown.

#### Scenario: Known allocated value survives candidate materialization
- **GIVEN** an indexed projection entry with logical size 12 MiB and allocated size 8 MiB marked known
- **WHEN** the entry is materialized as a search candidate and returned as a search result
- **THEN** the result SHALL retain logical size 12 MiB and allocated size 8 MiB as separate values

#### Scenario: Known zero is not unknown
- **GIVEN** an indexed entry whose allocated size is known to be zero
- **WHEN** it participates in Search
- **THEN** its result SHALL carry allocated-known=true and allocated-bytes=0

#### Scenario: Degraded candidate stays unknown
- **GIVEN** a search candidate produced from degraded directory metadata that has no allocated-size source
- **WHEN** it participates in Search
- **THEN** allocated size SHALL remain unknown and SHALL NOT be copied from logical size

### Requirement: Existing size filter remains logical size
The registered `size:` filter SHALL continue to evaluate logical `sizeBytes`. Adding allocated-size metadata SHALL NOT alter membership for existing `size:` queries and SHALL NOT implicitly register a new allocated-size query key.

#### Scenario: Logical and allocated values differ
- **GIVEN** a file with logical size 12 MiB and allocated size 8 MiB
- **WHEN** the user searches `size:>10MB`
- **THEN** the file SHALL match because the existing filter evaluates 12 MiB logical size

### Requirement: Size on Disk is a distinct explicit sort
The query/search engine SHALL support an explicit `Size on Disk` / allocated-size sort in addition to existing logical `Size` sorting.

Known allocated-size rows SHALL sort numerically. Unknown allocated-size rows SHALL sort after all known rows in both ascending and descending directions. Equal primary keys SHALL use the existing deterministic alphabetical secondary ordering.

#### Scenario: Ascending allocated-size sort
- **GIVEN** results with known allocated sizes 0 B, 4 KiB, and 8 KiB plus one unknown result
- **WHEN** results are sorted by Size on Disk ascending
- **THEN** the known rows SHALL appear in 0 B, 4 KiB, 8 KiB order and the unknown row SHALL appear after them

#### Scenario: Descending allocated-size sort keeps unknown last
- **GIVEN** results with known allocated sizes 0 B, 4 KiB, and 8 KiB plus one unknown result
- **WHEN** results are sorted by Size on Disk descending
- **THEN** the known rows SHALL appear in 8 KiB, 4 KiB, 0 B order and the unknown row SHALL still appear after them

#### Scenario: Logical Size sort is unchanged
- **GIVEN** two results whose logical-size order differs from their allocated-size order
- **WHEN** the caller selects existing `Size` sorting
- **THEN** ordering SHALL be based only on logical `sizeBytes` exactly as before this change

### Requirement: Allocated metadata does not add per-candidate I/O
Projection-backed allocated metadata SHALL be copied during the existing in-memory candidate traversal/materialization. Search SHALL NOT issue one database query, filesystem allocation query, privileged-service call, or extra IPC request per candidate or result to obtain Size on Disk.

#### Scenario: Broad indexed search
- **WHEN** a broad indexed search evaluates a large projection
- **THEN** allocated-size propagation SHALL use the metadata already present in each projection entry and SHALL preserve the existing lazy path reconstruction/cancellation architecture
