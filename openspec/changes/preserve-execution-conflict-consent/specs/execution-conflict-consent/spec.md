## ADDED Requirements

### Requirement: Unapproved transfers preserve destinations at execution

Copy, Move and Restore SHALL use a non-overwriting Shell collision policy for every item without explicit replacement consent. A destination appearing after preflight SHALL retain its data; the incoming item MAY receive a collision-safe generated name. The resulting Move history SHALL refer to the actual created destination.

#### Scenario: Destination appears while another collision is decided

- **WHEN** the first transfer target was absent at preflight and is created before a later conflict is answered
- **THEN** the new first destination remains unchanged
- **AND** the incoming item is retained at a collision-safe destination
- **AND** intentional Skip counts, actual completed paths and terminal outcome are truthful

#### Scenario: Directory appears with an existing nested name

- **WHEN** a planned absent folder destination appears before execution with a nested file matching the source name
- **THEN** the destination tree is not silently merged over or overwritten
- **AND** the incoming tree is preserved separately

### Requirement: Approved replacement and cancellation remain request scoped

Explicit Replace SHALL retain its approved behavior. Grouped Shell execution SHALL preserve one terminal request result, and cancellation or abort SHALL prevent a later replacement phase from starting. Destructive replacement SHALL NOT create a misleading reversible Move history.

#### Scenario: Mixed ordinary and approved replacement batch

- **WHEN** one request contains a noncolliding transfer and an approved replacement
- **THEN** both use their appropriate write-boundary policy
- **AND** skipped counts and completion are reported once for the original request

### Requirement: Terminal completion and undo track submitted roots

Successful descendant callbacks SHALL NOT inflate completion counts beyond the submitted selection. Actual root result paths SHALL own Move/recycle history; a descendant recycle identity SHALL NOT retarget a preceding root history entry. Child failures SHALL remain visible.

#### Scenario: Folder and skipped sibling

- **WHEN** a selected folder transfer emits both folder and descendant success callbacks while a second selected item is skipped
- **THEN** the request reports one completed root, one skipped root and total two
- **AND** Move undo contains only the actual root path

#### Scenario: Cancel ordinary phase before replacements

- **WHEN** a mixed request is cancelled after some ordinary files transfer but before the ordinary phase finishes
- **THEN** the approved replacement phase does not overwrite its destination
- **AND** terminal counts match the exact completed ordinary file inventory

### Requirement: Requested user cancellation is distinct from IO failure

Known Win32 ERROR_CANCELLED and Shell user-cancelled callback results SHALL NOT enter the IO failure list when cancellation was requested. Unrequested cancellation, access denial, sharing violations and generic or ambiguous abort results SHALL remain diagnosable failures.

#### Scenario: Accepted cancellation interrupts a native item

- **WHEN** the user requests cancellation and the native callback returns a known user-cancellation result
- **THEN** the request remains cancelled with its actual completed count
- **AND** cancellation alone does not create a stopped-with-errors failure notice

#### Scenario: Genuine failure accompanies cancellation

- **WHEN** an access or sharing failure is reported while cancellation was requested
- **THEN** the failure and original code remain visible
