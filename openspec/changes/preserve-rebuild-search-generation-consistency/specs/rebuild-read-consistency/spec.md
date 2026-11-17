## ADDED Requirements

### Requirement: Rebuild retires affected structural search captures before mutation

A nonempty rebuild MUST invalidate every search dependency whose projection slots, names, cursor layout or residency may be repacked, regardless of later rebuild/pruning success. An invalidation MUST NOT be interpreted as successful publication.

#### Scenario: Scoped accelerated search overlaps failed rebuild
- **WHEN** search captures filename candidate slots and RebuildAll repacks them before secondary pruning fails
- **THEN** search MUST discard those captures and recapture or use stable durable fallback, returning the exact canonical result rather than losing or retargeting rows

#### Scenario: Shared projection storage
- **WHEN** rebuilding one volume can repack other resident volumes' slots
- **THEN** their scoped captures MUST also be retired even if only another volume receives a successful rebuild callback

#### Scenario: Empty rebuild
- **WHEN** no durable volume is present
- **THEN** rebuild MUST NOT advance a structural epoch for a mutation that did not occur

### Requirement: Failed rebuild does not publish successful pruning

Reader invalidation MUST preserve failure reporting, durable/resident row retention and the existing bounded retry obligation.

#### Scenario: Secondary prune fails
- **WHEN** secondary pruning fails after projection reconstruction
- **THEN** the failed volume MUST NOT receive a success callback; its rows remain consistent with the durable store and a later retry can converge
