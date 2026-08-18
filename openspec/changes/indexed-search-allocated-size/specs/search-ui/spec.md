# indexed-search-allocated-size — search-ui delta

## MODIFIED Requirements

### Requirement: Result List with Core Metadata Columns
The result list SHALL display, for each result, the filename, item type (file or folder), containing folder/location path, logical size, Size on Disk, and last-modified time. Logical size and Size on Disk SHALL be visibly distinguishable. When Size on Disk is not known from the result's source, the row SHALL say that it is unknown rather than displaying zero or repeating logical size.

#### Scenario: Indexed result has known Size on Disk
- **WHEN** a search result carries logical size 12 MiB and known allocated size 8 MiB
- **THEN** the result row SHALL present both values distinctly and SHALL NOT label either value as the other

#### Scenario: Known zero allocation
- **WHEN** a result carries allocated-known=true and allocated-bytes=0
- **THEN** the result SHALL present Size on Disk as 0 B rather than Unknown

#### Scenario: Degraded result allocation is unknown
- **WHEN** a degraded Search result has no authoritative allocated-size metadata
- **THEN** the result row SHALL present Size on Disk as Unknown (or equivalent explicit wording) and SHALL remain otherwise usable

#### Scenario: Result row shows required metadata
- **WHEN** a search returns at least one matching entry
- **THEN** each row in the result list SHALL show that entry's filename, type, location path, logical size, Size on Disk, and last-modified time

#### Scenario: No matches found is clearly indicated
- **WHEN** a search completes and no entries match the current query and scope
- **THEN** the UI SHALL display an explicit "no results" indication, distinguishable from the state where a search is still in progress

### Requirement: Sortable Result List
In addition to the existing sort choices, the Search UI SHALL expose a distinct `Size on Disk` sort. Existing `Size` continues to mean logical size. The visible sort selection and direction SHALL reflect which dimension is active.

Unknown Size-on-Disk rows SHALL remain after known rows in both ascending and descending modes, consistent with the query engine's allocated-size sort contract.

#### Scenario: User selects Size on Disk
- **WHEN** the user selects `Size on Disk` from the Search sort control
- **THEN** Search SHALL request/apply the allocated-size sort and the control SHALL visibly show that `Size on Disk` rather than logical `Size` is active

#### Scenario: Existing Size sort remains logical
- **WHEN** the user selects `Size`
- **THEN** Search SHALL sort by logical size exactly as before this change

#### Scenario: Changing the sort field reorders results
- **WHEN** a user selects a sort field other than relevance (for example, logical size or Size on Disk)
- **THEN** the result list SHALL reorder according to that field and SHALL visibly indicate the newly active sort field and direction

#### Scenario: Default sort is relevance
- **WHEN** a user performs a search without having changed the sort field
- **THEN** the result list SHALL be ordered by relevance by default

## ADDED Requirements

### Requirement: Search allocated metadata is virtualized with the existing row model
Size-on-Disk metadata SHALL be stored in the same bounded Search result row model used for owner-data rendering. The UI SHALL NOT create a persistent child control per result or re-query the engine/filesystem during paint.

#### Scenario: Large result set
- **WHEN** a large result set is displayed and the viewport is scrolled
- **THEN** Size-on-Disk metadata SHALL render from the existing result row state with the same virtualization behavior as filename, path, logical size, and date

### Requirement: Allocated metadata survives search cancellation and replacement safely
A superseded query's Size-on-Disk values SHALL never appear in the result rows of the replacement query. Result replacement SHALL invalidate the old result state under the same cancellation/sequence rules used for the existing fields.

#### Scenario: Query changes while indexed response is in flight
- **WHEN** an old query's response arrives after the current query has changed
- **THEN** neither its logical metadata nor its Size-on-Disk metadata SHALL be applied to the current result list
