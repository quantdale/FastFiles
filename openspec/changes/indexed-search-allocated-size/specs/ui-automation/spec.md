# indexed-search-allocated-size — ui-automation delta

## ADDED Requirements

### Requirement: Search result UIA exposes the same size semantics as the visible row
The existing semantic `Search.Results` ListItems SHALL expose both logical size and Size on Disk from the same Search result state used by visible rendering. The accessible representation SHALL preserve allocated known/zero/unknown semantics and SHALL not create a parallel accessibility-only metadata source.

#### Scenario: Known allocated value
- **GIVEN** a visible Search row with logical size 12 MiB and known Size on Disk 8 MiB
- **WHEN** a UIA client reads that result ListItem's accessible metadata
- **THEN** it SHALL be able to distinguish logical 12 MiB from on-disk 8 MiB

#### Scenario: Known zero versus unknown
- **WHEN** one Search result has allocated-known=true with 0 B and another has allocated-known=false
- **THEN** their accessible metadata SHALL report 0 B on disk for the first and Unknown for the second

### Requirement: Search UIA hierarchy remains virtualized and stable
Adding Size-on-Disk metadata SHALL NOT add duplicate child elements or change the established `Search.Results` fragment-root hierarchy, result runtime identity, `SelectionItem`, `Invoke`, `ScrollItem`, `Selection`, or `ItemContainer` contracts.

#### Scenario: Large Search result set
- **WHEN** a UIA client enumerates or looks up results in a large virtualized Search result set
- **THEN** providers SHALL remain on-demand and bounded by the existing cache policy; no extra child provider SHALL exist solely to represent Size on Disk

### Requirement: Allocated metadata follows stale-result safety
A Search result provider held across query replacement SHALL obey the existing stale-element contract. It SHALL NOT retarget to a new result at the same virtual index and SHALL NOT retain/reveal Size-on-Disk metadata from the superseded query as though it belonged to the new result.

#### Scenario: Query replacement
- **GIVEN** a client holds a provider for a result with known allocated size
- **WHEN** a new query replaces the result set
- **THEN** the held provider SHALL fail cleanly under the existing stale-provider contract rather than retargeting or exposing stale metadata

### Requirement: UIA property reads do not perform metadata I/O
Reading Search result allocated-size semantics through UIA SHALL use the result's in-memory semantic projection only. It SHALL NOT perform filesystem allocation queries, engine IPC, privileged-service calls, or database queries from UIAutomationCore callback threads.

#### Scenario: UIA client reads many result descriptions
- **WHEN** a client queries accessible metadata for many visible Search results
- **THEN** those reads SHALL be bounded in-memory operations and SHALL not introduce per-item I/O
