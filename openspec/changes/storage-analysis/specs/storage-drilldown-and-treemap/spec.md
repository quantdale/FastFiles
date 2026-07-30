## ADDED Requirements

### Requirement: Hierarchical Drill-Down with Percentage Context
The system SHALL present a hierarchical drill-down of a selected volume or folder scope in which every listed item shows its size, its percentage of its immediate parent, and its percentage of the overall volume, at every level of the hierarchy.

#### Scenario: Drilling into a subdirectory
- **WHEN** a user drills into a folder within the storage drill-down view
- **THEN** the system SHALL display that folder's children with each child's size, its percentage of the immediate parent folder, and its percentage of the overall volume

#### Scenario: Percentage context recalculates at each level
- **WHEN** a user drills further into a child folder
- **THEN** percentage-of-parent SHALL recompute relative to the newly entered folder, while percentage-of-volume SHALL continue to reflect the total size of the whole volume

### Requirement: Largest Folders View
The system SHALL provide a view listing folders within a selected volume or folder scope ranked by descending aggregate size.

#### Scenario: Viewing largest folders in a scope
- **WHEN** a user opens the largest-folders view for a volume or a chosen folder scope
- **THEN** the system SHALL display folders within that scope ranked by descending aggregate size

### Requirement: Largest Files View
The system SHALL provide a view listing individual files within a selected volume or folder scope ranked by descending size.

#### Scenario: Viewing largest files in a scope
- **WHEN** a user opens the largest-files view for a volume or a chosen folder scope
- **THEN** the system SHALL display individual files within that scope ranked by descending size

### Requirement: Sorting Storage Views
Drill-down, largest-folders, and largest-files listings SHALL support sorting by size, name, type, and last-modified date, in either ascending or descending order.

#### Scenario: Sorting a listing by size
- **WHEN** a user selects size as the sort field for a drill-down, largest-folders, or largest-files listing
- **THEN** the system SHALL reorder the listing by size and SHALL allow toggling between ascending and descending order

#### Scenario: Sorting a listing by name, type, or date
- **WHEN** a user selects name, type, or last-modified date as the sort field
- **THEN** the system SHALL reorder the listing accordingly, with ascending/descending toggling available as with size

### Requirement: Interactive Treemap Visualization
The system SHALL render an interactive treemap for the current volume or folder scope using a squarified layout algorithm, where each visible item's rectangle area is proportional to its size relative to its siblings within the same container.

#### Scenario: Treemap reflects proportional sizes
- **WHEN** the storage drill-down view is opened for a scope whose item sizes are known
- **THEN** the system SHALL render a treemap in which each visible item's rectangle area is proportional to its size relative to its siblings within the same container

### Requirement: Treemap Hover Metadata
Hovering over a rendered treemap rectangle SHALL display that item's full path, size, percentage of its parent, and percentage of the volume, without requiring a click.

#### Scenario: Hovering over a rectangle shows metadata
- **WHEN** a user hovers the pointer over a rendered treemap rectangle
- **THEN** the system SHALL display that item's full path, size, percentage of its parent, and percentage of the volume, updating as the pointer moves to a different rectangle

### Requirement: Treemap Click-to-Navigate
Clicking a treemap rectangle SHALL navigate the user into that item's location: a folder rectangle drills the treemap and drill-down view into that folder, and a file rectangle opens the standard file-browsing view at that file's containing folder with the file selected.

#### Scenario: Clicking a folder rectangle drills down
- **WHEN** a user clicks a rectangle representing a folder
- **THEN** the treemap and drill-down view SHALL descend into that folder's contents, updating the displayed percentage context accordingly

#### Scenario: Clicking a file rectangle opens the file browser
- **WHEN** a user clicks a rectangle representing an individual file
- **THEN** the system SHALL open or focus the standard column-view file-browsing surface at that file's containing folder with the file selected, without performing any file operation on it

### Requirement: Graceful Calculating State for Unknown Sizes
When an item's or subtree's aggregate size is not yet known, the drill-down and treemap views SHALL display an explicit "Calculating…" affordance for that item rather than a blank, zero, or otherwise misleading value, and SHALL NOT block interaction with the rest of the view while the calculation completes.

#### Scenario: Unresolved size shows a calculating state
- **WHEN** a user views an item whose aggregate size has not yet been computed, such as during initial indexing
- **THEN** the system SHALL display a "Calculating…" indicator for that item's size instead of a blank, zero, or misleading value, and SHALL NOT block navigation or interaction with the rest of the drill-down or treemap view

#### Scenario: Pending size resolves in place
- **WHEN** a previously "Calculating…" item's aggregate size becomes available
- **THEN** the system SHALL update that item's displayed size and any dependent percentages in place, without requiring the user to reopen or manually refresh the view

### Requirement: Scope Reflects Index Coverage
When whole-volume index coverage is not available, the drill-down and treemap views SHALL be limited to indexed (browsed or pinned) directories and SHALL clearly label the displayed scope as partial rather than presenting it as complete whole-volume coverage.

#### Scenario: Partial coverage is clearly labeled
- **WHEN** the underlying index for the current volume covers only browsed or pinned directories rather than the whole volume
- **THEN** the drill-down and treemap views SHALL clearly label the displayed data as partial coverage rather than presenting it as a complete whole-volume picture

### Requirement: Non-Destructive Storage Analysis
No interaction within the drill-down or treemap views SHALL create, delete, move, rename, or otherwise modify any file or folder; any destructive or mutating action reachable from these views SHALL be handed off as a separate, explicit action to `file-operations-core`.

#### Scenario: Read-only exploration never mutates state
- **WHEN** a user hovers, clicks to navigate, sorts, or drills through the drill-down or treemap views
- **THEN** none of those interactions SHALL create, delete, move, rename, or otherwise modify any file or folder

#### Scenario: Destructive action is handed off, not performed in place
- **WHEN** a user chooses a delete or move action while viewing the drill-down or treemap
- **THEN** the system SHALL hand off to `file-operations-core`'s own confirmation and execution flow as a separate, explicit action, and SHALL NOT delete, move, or modify the item directly from within storage-drilldown-and-treemap
