# status-bar Specification

## Purpose
A persistent, read-only status bar showing selection count, selection size, and current path, updated on selection and navigation changes.

## Requirements
### Requirement: Persistent Status Bar Display
`FastFiles` SHALL display a persistent status bar, always visible during Column View browsing, showing the number of currently selected items, the total size of the current selection, and the current path.

#### Scenario: Status bar is visible during normal browsing
- **WHEN** the user is browsing in Column View, regardless of what is or isn't selected
- **THEN** the status bar SHALL be visible and SHALL display a selection count, a total selection size, and the current path

### Requirement: Status Bar Updates on Selection Change
The status bar SHALL update its displayed selection count and total selection size whenever the current selection changes.

#### Scenario: Selecting an additional item updates the counts
- **WHEN** the user extends the current selection to include an additional item (for example, via Ctrl-click)
- **THEN** the status bar SHALL update to reflect the new selection count and the new total selection size

#### Scenario: Clearing the selection updates the display
- **WHEN** the user's selection is cleared to zero items
- **THEN** the status bar SHALL update to reflect a selection count of zero and a total selection size of zero

### Requirement: Status Bar Updates on Navigation Change
The status bar SHALL update its displayed current path whenever the user navigates to a different location, independently of whether the selection also changed.

#### Scenario: Navigating to a new folder updates the displayed path
- **WHEN** the user navigates to a different folder in Column View
- **THEN** the status bar SHALL update its displayed current path to reflect the new location, regardless of whether the selection changed as a result

### Requirement: Zero-Selection Default State
When no items are selected, the status bar SHALL display a selection count of zero and a total selection size of zero, alongside the current path.

#### Scenario: No items selected shows a zero count and size
- **WHEN** the user has navigated to a folder without selecting any item within it
- **THEN** the status bar SHALL display a selection count of zero, a total selection size of zero, and the current path

### Requirement: Status Bar Is a Read-Only, Non-Owning Display
The status bar SHALL only read and display existing selection and navigation state owned by `column-view-browsing` and `multi-selection-and-dragdrop`; it SHALL NOT own, mutate, or independently compute that state, and it SHALL NOT itself trigger new asynchronous folder-size computations.

#### Scenario: Status bar reflects only already-known size information
- **WHEN** the current selection includes a folder whose total size is not yet known from the index and has not yet been resolved by an asynchronous computation triggered elsewhere in the application
- **THEN** the status bar's displayed total selection size SHALL reflect only the items whose size is already known, and the status bar SHALL NOT itself initiate a computation to resolve the unknown folder's size
