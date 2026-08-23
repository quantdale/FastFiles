# Memory-Bounded Indexed Scale

## filesystem-index-store Delta

## MODIFIED Requirements

### Requirement: Compact In-Memory Projection

The projection SHALL remain a bounded working set: when its estimated memory
exceeds the configured maximum, subtree eviction SHALL reduce it, evicted data
SHALL remain durable in the store, and the eviction/reload policy SHALL be
observable through testable diagnostics. No read path (search, browse,
aggregate, snapshot export) MAY reconstruct the complete durable index in RAM
merely to answer a query.

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

Folder aggregates SHALL be answerable for hot volumes from the in-memory
projection walk and for evicted volumes from the durable store's recursive
aggregate, without loading the volume into memory. Both paths SHALL report the
item count, logical total, allocated total, and the allocated known/total
completeness pair; an aggregate SHALL never be fabricated from partial
residency alone.

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
