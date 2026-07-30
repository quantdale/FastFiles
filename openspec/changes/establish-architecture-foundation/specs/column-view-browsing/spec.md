## ADDED Requirements

### Requirement: Multi-Column Hierarchy Display
`FastFiles` SHALL display the filesystem hierarchy as multiple adjacent columns, where each column represents one level of the hierarchy, so a user can see a parent folder, the selected folder, and the selected folder's contents simultaneously.

#### Scenario: Selecting a folder populates the next column
- **WHEN** a user selects a folder in a column
- **THEN** a new column SHALL appear to its right showing that folder's contents, while all preceding columns remain visible and unchanged

#### Scenario: Selecting a different folder replaces later columns
- **WHEN** a user selects a different folder within a column that already has a populated column to its right
- **THEN** the columns to the right of the newly selected folder SHALL be replaced with the newly selected folder's contents, and columns to its left SHALL remain unchanged

### Requirement: File and Folder Visual Distinction
Each item in a column SHALL be visually distinguishable as a file or a folder, and only folders SHALL populate a subsequent column when selected.

#### Scenario: Selecting a file does not create a new column
- **WHEN** a user selects a file (not a folder) within a column
- **THEN** no new column SHALL be created to its right

### Requirement: Selection State Visibility
The currently selected item within each column SHALL be visually distinguished from unselected siblings, and the column representing the deepest active selection SHALL be identifiable.

#### Scenario: Selected item is visually distinguished
- **WHEN** an item is selected within a column
- **THEN** that item SHALL be rendered with a distinct visual state from other items in the same column

### Requirement: Horizontal Scrolling for Deep Hierarchies
When the number of populated columns exceeds the available window width, the column view SHALL provide horizontal scrolling rather than compressing columns to illegibility or hiding navigation.

#### Scenario: Navigating deeper than the viewport width
- **WHEN** the user navigates to a depth where the total column width exceeds the visible window width
- **THEN** the view SHALL become horizontally scrollable, keeping the most recently populated column visible

### Requirement: Graceful Handling of Inaccessible or Changed Directories
The column view SHALL handle permission-denied and no-longer-existing directories by showing a clear in-place message, without crashing or leaving the UI in an inconsistent state.

#### Scenario: Permission-denied folder shows an in-column message
- **WHEN** a user selects a folder they do not have permission to read
- **THEN** the resulting column SHALL display a clear access-denied message instead of an empty or crashed state

#### Scenario: A directory disappears between listing and selection
- **WHEN** a user selects a folder that was listed but has since been deleted or moved
- **THEN** the column view SHALL display a clear "no longer available" message for that item and SHALL NOT crash or hang
