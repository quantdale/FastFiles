## ADDED Requirements

### Requirement: Maximum projection size
The in-memory projection SHALL enforce a configurable maximum size. When the limit is exceeded, the engine SHALL evict the least-recently-accessed directory entries until the projection is under the limit.

#### Scenario: Projection grows beyond limit
- **WHEN** the projection reaches 500 MB and a new directory is accessed
- **THEN** the engine evicts the least-recently-accessed entries until the projection is below 500 MB

#### Scenario: Evicted entry reload
- **WHEN** the user navigates to a directory that was previously evicted
- **THEN** the engine reloads the directory data from the SQLite store and updates the snapshot
