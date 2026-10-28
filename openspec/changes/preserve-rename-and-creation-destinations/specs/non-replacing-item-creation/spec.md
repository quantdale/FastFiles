## ADDED Requirements

### Requirement: Rename preserves unapproved destination data
The unprivileged file-operation worker SHALL rename only to the exact requested sibling name and SHALL NOT replace an existing destination, including one created after validation. Successful renames SHALL preserve FastFiles reversible history; failed renames SHALL report the native failure without a success history entry.

#### Scenario: Destination appears after validation
- **WHEN** a destination is absent during validation but exists at the mutation boundary
- **THEN** the production rename primitive fails and preserves both source and destination bytes

#### Scenario: Case-only rename
- **WHEN** the requested sibling name differs only in case from the source on a normal case-insensitive directory
- **THEN** rename succeeds with truthful resulting-path history

### Requirement: New item success identifies newly created objects only
The worker SHALL exclusively create a valid new empty file or directory. An existing destination SHALL be reported as a failure, with zero completed creations and no createdPaths entry, and its content and descendants SHALL remain unchanged.

#### Scenario: Existing file or directory
- **WHEN** creation is requested using an occupied destination name
- **THEN** existing bytes and children remain intact and the item is not presented for new-item inline rename

#### Scenario: Fresh creation and recovery
- **WHEN** a valid unoccupied sibling name is requested after a collision
- **THEN** the new item is created successfully and its exact path is returned
