# indexed-search-allocated-size — index-engine delta

## ADDED Requirements

### Requirement: Indexed Search response preserves allocated-size knownness
When `FastFilesEngine` serves projection-backed indexed Search, each extended result candidate SHALL preserve the projection entry's allocated/on-disk byte count and explicit known/unknown state in addition to existing logical size and identity metadata.

#### Scenario: Known allocated metadata reaches the UI boundary
- **GIVEN** a projection entry whose allocated size is known
- **WHEN** `SearchIndexed` returns that entry and the engine materializes the UI response
- **THEN** the response candidate SHALL carry the exact allocated byte count with known=true

#### Scenario: Unknown allocated metadata reaches the UI boundary
- **GIVEN** a candidate whose allocated size is unknown
- **WHEN** the engine materializes the Search response
- **THEN** the response SHALL preserve unknown and SHALL NOT substitute logical size

### Requirement: Indexed Search protocol extension is additive
The engine/UI Search protocol SHALL preserve the exact legacy indexed-search result layout and add allocated metadata through a distinct additive/versioned representation. Existing message IDs, fixed legacy candidate layout, and old-sort request bytes SHALL remain unchanged.

A decoder receiving a legacy response SHALL treat allocated size as unknown. Extended response parsing SHALL remain exact, length-bounded, count-bounded, overflow-checked, and fail closed on malformed flags/reserved fields.

#### Scenario: Legacy response is decoded by the new UI
- **WHEN** the new parser receives a valid historical indexed-search result that contains only logical size
- **THEN** all existing fields SHALL decode exactly as before and allocated-known SHALL be false

#### Scenario: Extended response carries known zero
- **WHEN** the engine emits an extended candidate with allocated-known=true and allocated-bytes=0
- **THEN** the new UI SHALL decode a known zero rather than Unknown

#### Scenario: Legacy byte contract remains pinned
- **WHEN** protocol regression tests serialize an existing pre-change indexed-search request/result using only legacy fields/sorts
- **THEN** the bytes SHALL remain identical to the pinned historical representation

### Requirement: Size on Disk sort is validated by the engine protocol boundary
If the indexed-search request sort enum is extended with Size on Disk, the new raw value SHALL be explicitly validated and mapped to the allocated-size sort. All existing sort values SHALL retain their prior numeric and semantic meaning.

#### Scenario: Existing Size sort arrives over IPC
- **WHEN** an indexed-search request uses the existing logical Size sort value
- **THEN** the engine SHALL continue to sort by logical size

#### Scenario: New Size on Disk sort arrives over IPC
- **WHEN** an indexed-search request uses the new allocated-size sort value
- **THEN** the engine SHALL apply the search engine's Size-on-Disk ordering contract

### Requirement: Extended Search metadata does not expand the privileged boundary
Allocated Search metadata SHALL come only from state already owned by the unprivileged engine/index projection. This change SHALL NOT add raw-volume reads, `$DATA` parsing, filesystem-content reads, service-side search, or new privileged RPCs.

#### Scenario: Privileged service unavailable
- **WHEN** FastFiles operates without a validated privileged service
- **THEN** Search remains functional; candidates without allocated metadata report it as unknown, and no production-signing/privileged validation is required to exercise the feature safely
