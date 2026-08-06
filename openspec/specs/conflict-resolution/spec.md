# conflict-resolution Specification

## Purpose
In-app handling of destination naming conflicts during copy/move batches: detection up front, Replace/Skip/Keep Both choices with apply-to-all, and consistent per-item decisions.

## Requirements
### Requirement: Conflict Detection Before Batch Execution
Before executing a copy or move batch operation, `FastFiles` SHALL check whether any item in the batch would collide with an existing item of the same name at the destination, and SHALL identify every such conflict prior to transferring any conflicting item's data.

#### Scenario: A batch contains one conflicting item among several
- **WHEN** a user copies a selection of five items into a destination folder where one of the five names already exists
- **THEN** FastFiles SHALL identify that one item as a conflict before copying it, while the other four non-conflicting items proceed without requiring a decision

### Requirement: Custom In-App Conflict Dialog
`FastFiles` SHALL present destination-naming conflicts through a custom, in-app dialog styled consistently with the rest of the FastFiles UI, rather than the native Explorer/shell conflict dialog. The dialog SHALL clearly identify the conflicting item and offer Replace, Skip, and Keep Both (automatic rename) as choices.

#### Scenario: Conflict dialog appearance
- **WHEN** a naming conflict is detected during a copy or move operation
- **THEN** FastFiles SHALL display its own in-app conflict dialog, visually consistent with FastFiles' UI, rather than launching a native shell conflict dialog

### Requirement: Replace Resolution
`FastFiles` SHALL support resolving a conflict by replacing the existing destination item with the incoming item, when the user selects Replace for that conflict.

#### Scenario: User chooses Replace
- **WHEN** a user selects Replace for a conflicting item
- **THEN** the existing destination item SHALL be overwritten by the incoming item

### Requirement: Skip Resolution
`FastFiles` SHALL support resolving a conflict by leaving the existing destination item untouched and not transferring the incoming item, when the user selects Skip for that conflict.

#### Scenario: User chooses Skip
- **WHEN** a user selects Skip for a conflicting item
- **THEN** the existing destination item SHALL remain unchanged and the incoming item SHALL NOT be copied or moved to that destination

### Requirement: Automatic Rename (Keep Both) Resolution
`FastFiles` SHALL support resolving a conflict by automatically generating a non-colliding destination name (e.g., appending " (2)") for the incoming item, when the user selects Keep Both for that conflict, so that both the existing and incoming items are preserved.

#### Scenario: User chooses Keep Both
- **WHEN** a user selects Keep Both for a conflicting item
- **THEN** FastFiles SHALL generate a non-conflicting destination name and transfer the incoming item under that new name, leaving the existing destination item unchanged

### Requirement: Apply to All Remaining Conflicts
`FastFiles` SHALL allow a user to mark their chosen resolution (Replace, Skip, or Keep Both) to apply automatically to all remaining conflicts within the same batch operation, without prompting again for each subsequent conflict.

#### Scenario: User applies a resolution to all remaining conflicts
- **WHEN** a user selects a resolution for a conflict and checks "Apply to all remaining"
- **THEN** every subsequent conflict within that same batch operation SHALL be resolved automatically using the same chosen resolution, without displaying the conflict dialog again for that batch

#### Scenario: Apply-to-all choice does not carry over to a new operation
- **WHEN** a batch operation with an active "Apply to all remaining" choice completes and the user later initiates a new, separate copy or move operation
- **THEN** the new operation SHALL prompt for conflict resolution independently, unaffected by the previous operation's apply-to-all choice

### Requirement: Consistent Resolution Across a Batch
Within a single batch operation, `FastFiles` SHALL apply conflict-resolution decisions consistently: once a resolution is chosen for a specific conflicting item, that same decision SHALL govern that item for the remainder of the operation, and SHALL NOT be re-prompted for that same item.

#### Scenario: A resolved conflict is not re-prompted
- **WHEN** a user has already chosen a resolution for a specific conflicting item within a batch
- **THEN** FastFiles SHALL NOT show the conflict dialog again for that same item within that same batch operation
