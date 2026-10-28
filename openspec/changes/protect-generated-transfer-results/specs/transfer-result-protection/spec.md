## Purpose
Protect incoming file-operation results from replacement work later in the same request, while retaining truthful partial outcomes and recoverable incoming sources.

## ADDED Requirements

### Requirement: Replacement preserves observed incoming results
For Copy, Move and Restore, FastFiles SHALL prevent an approved replacement phase from overwriting a callback-observed completed ordinary result at the same Windows lexical path or an ancestor/descendant path. An unavailable observed output name SHALL prevent establishing replacement safety.

#### Scenario: A vacated approved name becomes an ordinary result
- **WHEN** an ordinary transfer receives a vacated approved replacement name after preflight
- **THEN** the incoming ordinary result SHALL survive and the replacement source SHALL remain available

#### Scenario: Unrelated approved replacement
- **WHEN** no approved target overlaps an observed ordinary result namespace
- **THEN** the approved replacement SHALL retain its ordinary supported behavior

### Requirement: Refused replacement group reports partial outcomes
If a queued replacement group cannot execute safely, FastFiles SHALL report each queued replacement as a failure, preserve its incoming source, and retain the ordinary completed count without reporting full success, cancellation or an intentional skip.

#### Scenario: An unrelated replacement shares the refused group
- **WHEN** one approved target overlaps an observed ordinary result and another target does not
- **THEN** the group SHALL preserve both incoming replacement sources and report both refusals explicitly
