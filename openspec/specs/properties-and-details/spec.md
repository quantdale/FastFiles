# properties-and-details Specification

## Purpose
The in-app properties panel: full single-item property sets, index-sourced or asynchronously computed folder aggregates with a visible calculating state, multi-selection aggregates, and preservation of navigation context.

## Requirements
### Requirement: In-App File Properties Display
When a single file is selected, `FastFiles` SHALL display, within its own in-app properties panel, the file's name, extension, size, created/modified/accessed timestamps, full path, attributes, and a file-type description.

#### Scenario: Selecting a single file shows its full property set
- **WHEN** the user selects a single file
- **THEN** the properties panel SHALL display that file's name, extension, size, created/modified/accessed timestamps, full path, attributes, and file-type description

### Requirement: In-App Folder Properties Display
When a single folder is selected, `FastFiles` SHALL display, within its own in-app properties panel, that folder's total item count and total size.

#### Scenario: Selecting a single folder shows its aggregate properties
- **WHEN** the user selects a single folder
- **THEN** the properties panel SHALL display that folder's total item count and total size

### Requirement: Index-Sourced Folder Metadata When Already Known
When a selected folder's item count and total size are already fully known from `filesystem-index-store`, the properties panel SHALL display those values directly without triggering a new computation.

#### Scenario: Fully indexed folder shows immediate metadata
- **WHEN** the user selects a folder whose subtree is already fully indexed and its item count/total size are known to the index
- **THEN** the properties panel SHALL display the index-sourced item count and total size without a "Calculating…" state

### Requirement: Asynchronous Folder Metadata Computation With Visible Calculating State
When a selected folder's item count and total size are not already known from the index, `FastFiles` SHALL request that they be computed asynchronously, SHALL display a visible "Calculating…" state immediately while the computation is in progress, and SHALL update the display with the result once it becomes available — without blocking navigation, selection changes, or any other input while the computation runs.

#### Scenario: Unindexed folder shows a calculating state that resolves
- **WHEN** the user selects a folder whose item count/total size are not yet known from the index
- **THEN** the properties panel SHALL immediately display a "Calculating…" state, the UI SHALL remain fully responsive to navigation and selection input while the computation runs, and the panel SHALL update to show the computed item count and total size once the computation completes

#### Scenario: Navigating away during computation does not apply a stale result
- **WHEN** the user changes the selection or navigates elsewhere before an in-flight folder metadata computation completes
- **THEN** the properties panel SHALL NOT apply that computation's result to a selection it no longer corresponds to when it later completes

### Requirement: Multi-Selection Shows Aggregate Properties
When more than one item is selected, the properties panel SHALL display an aggregate view — total item count and total size across the selection — rather than single-item detail fields that do not generalize across multiple items.

#### Scenario: Selecting multiple items shows a count and total size
- **WHEN** the user selects more than one file or folder at once
- **THEN** the properties panel SHALL display the total number of selected items and their combined total size, rather than per-item fields such as a single creation date or path

### Requirement: Properties View Preserves Navigation Context
The properties/details view SHALL be an in-app panel embedded within `FastFiles`'s own window, and SHALL NOT open Explorer's native Properties dialog or any other separate OS-owned window.

#### Scenario: Viewing properties does not leave the app's navigation context
- **WHEN** the user views properties for a selected file or folder
- **THEN** the properties SHALL be shown within `FastFiles`'s own window, and the user's current Column View navigation state SHALL remain visible and unaffected
