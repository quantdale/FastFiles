# allocated-size-search-filter — search-ui delta

## MODIFIED Requirements

### Requirement: Search-as-You-Type with Debounced Execution
The Search input SHALL accept the registered `ondisk:` structured filter through the same debounced query path as existing structured filters. No separate execution path, button, or modal UI is introduced.

#### Scenario: Size on Disk query is dispatched normally
- **WHEN** a user types `ondisk:>10MB` and pauses past the existing debounce interval
- **THEN** Search SHALL dispatch the query through the normal current scope/source and SHALL NOT require an additional action

### Requirement: Result List with Core Metadata Columns
When an `ondisk:` query runs against a source whose candidates have unknown allocated size, those UNKNOWN candidates SHALL not be fabricated into numeric matches. The ordinary no-results state remains valid when no candidate can satisfy the known-value predicate.

#### Scenario: Degraded unknown results do not become numeric matches
- **WHEN** the current degraded Search source contains usable results whose allocated size is unknown and the user enters `ondisk:>=0B`
- **THEN** those candidates SHALL not appear merely because their logical sizes are nonnegative or because zero is used as an absence sentinel

## ADDED Requirements

### Requirement: Size on Disk filter participates in existing query feedback
The Search UI SHALL treat `ondisk` as a recognized structured filter key. Existing invalid-filter and unrecognized-key feedback, if visible in the current UI, SHALL distinguish a malformed `ondisk` value from an unknown filter key without requiring a new feedback surface.

#### Scenario: Registered key is not reported as unrecognized
- **WHEN** the user enters a syntactically valid `ondisk:` query
- **THEN** the UI SHALL NOT label `ondisk` as an unrecognized key

#### Scenario: Malformed on-disk value follows existing invalid-filter behavior
- **WHEN** the user enters a malformed value such as `ondisk:banana`
- **THEN** the UI SHALL follow the existing invalid structured-filter behavior and SHALL NOT silently reinterpret the query as a filename literal

### Requirement: Query replacement remains source-consistent with Size on Disk predicates
Switching between a normal query, a numeric `ondisk:` query, and a replacement query SHALL continue to use the existing generation/cancellation rules. Results from a superseded query SHALL never reappear after the query text changes.

#### Scenario: Degraded fixture is restored after an on-disk query
- **WHEN** a normal query finds deterministic degraded fixture rows, the user changes to `ondisk:>=0B` (which correctly matches none because allocation is unknown), and then restores the normal query
- **THEN** the original fixture rows SHALL reappear from the current query only, with no stale `ondisk:` completion corrupting the list
