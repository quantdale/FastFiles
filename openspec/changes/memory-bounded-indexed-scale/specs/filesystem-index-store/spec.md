# Memory-Bounded Indexed Scale

## filesystem-index-store Delta

## MODIFIED Requirements

### Requirement: Compact In-Memory Projection
`FastFilesEngine` SHALL maintain an in-memory projection of the index in which each entry is a fixed-size record referencing its parent by `FileReferenceNumber` (or an equivalent dense internal reference) and its name by an interned/deduplicated string id, rather than by a stored full path string. This projection, not the durable store, is what is published to UI clients per the `index-engine` capability's snapshot-publication mechanism. The projection SHALL remain a bounded working set: when its estimated memory exceeds the configured maximum, subtree eviction SHALL reduce it, evicted data SHALL remain durable in the store, and the eviction/reload policy SHALL be observable through testable diagnostics. No read path (search, browse, aggregate, snapshot export) MAY reconstruct the complete durable index in RAM merely to answer a query.

#### Scenario: Sibling entries with long shared ancestor paths do not duplicate that path in memory
- **WHEN** the projection holds many entries under a common, deeply-nested ancestor directory
- **THEN** the shared ancestor path SHALL NOT be stored redundantly per entry; each entry SHALL reference its parent rather than storing a materialized path

#### Scenario: Repeated file/directory names across the tree share one interned string
- **WHEN** the same name (e.g., a common filename or directory name) occurs under many different parents across the indexed volume
- **THEN** the projection SHALL store that name once in its interned string pool and reference it by id from every entry with that name, rather than storing the string once per entry

#### Scenario: A full path is reconstructed only when needed
- **WHEN** a UI client or engine-internal consumer needs an entry's full path (e.g., for display or to open the file)
- **THEN** the engine SHALL reconstruct it by walking parent references and concatenating interned names on demand, rather than reading a precomputed path field

#### Scenario: Forced eviction keeps the memory ceiling
- **Given** a store with more entries than a deliberately small projection budget
- **When** ingestion and queries run repeatedly against that budget
- **Then** the projection's estimated memory never persistently exceeds the configured maximum
- **And** the eviction count is bounded by the number of limit-enforcing mutations, not by query count

#### Scenario: Diagnostics report the memory policy
- **Given** any projection state
- **When** diagnostics are requested
- **Then** they report the configured maximum, estimated bytes, bytes before/after the last eviction, and monotonic subtree-eviction and cold-reload counts

### Requirement: Folder Aggregates Report Both Totals with Completeness
(allocated-size-storage-accuracy) A folder aggregate SHALL report the item count, the logical-size total, the allocated-size subtotal over descendants whose allocated size is known, and an explicit completeness pair (known count / total count). Arithmetic SHALL be checked, saturating at `UINT64_MAX` with the aggregate marked incomplete on saturation, so a partial subtotal is never presented as an exact total. A folder aggregate SHALL be answerable for hot volumes from the in-memory projection walk and for evicted volumes from the durable store's recursive aggregate, without loading the volume into memory.

#### Scenario: All descendants known yields a complete allocated total
- **WHEN** every descendant of a folder has a known allocated size
- **THEN** the aggregate SHALL report the exact allocated total and mark the allocated portion complete

#### Scenario: Unknown descendants yield an explicit partial aggregate
- **WHEN** some descendants' allocated size is unknown
- **THEN** the aggregate SHALL report the known subtotal plus the known/total counts, and SHALL NOT claim the subtotal is the folder's full on-disk footprint

#### Scenario: Evicted-volume aggregate matches the durable data
- **Given** an indexed volume whose projection residency has been evicted under a small budget
- **When** a folder inside the evicted volume is aggregated
- **Then** the returned totals equal the durable store's recursive CTE result
- **And** no whole-volume reload into the projection occurs

## ADDED Requirements

### Requirement: Cold Data Is Never Absent

Every consumer of projection data — indexed search, folder aggregates,
candidate path reconstruction, directory-snapshot export — SHALL treat an
evicted (cold) volume as durable-but-not-resident and SHALL answer from the
durable store. No consumer MAY interpret "evicted" as "does not exist".

#### Scenario: Search finds a match inside an evicted subtree

- **Given** a matching file resides in a subtree evicted by the memory policy
- **When** an indexed search runs whose only match is that file
- **Then** the file is returned with correct name, folder, sizes, and identity

#### Scenario: Path reconstruction works across the hot/cold boundary

- **Given** a matched candidate's parent chain crosses evicted subtrees
- **When** its display folder is materialized
- **Then** the full canonical folder is reconstructed from durable rows without loading the whole volume

### Requirement: Durable Point Lookup

The store SHALL provide a point lookup of one entry by (volume, FileReferenceNumber) so cold parent-chain walks cost O(depth) bounded SQLite reads instead of full-volume streams.

#### Scenario: Point lookup returns the persisted row

- **Given** an entry previously committed through the normal ingestion batch path
- **When** the entry is looked up by volume and FileReferenceNumber
- **Then** every persisted field round-trips exactly, including the explicit allocated-size unknown flag
