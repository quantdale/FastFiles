## ADDED Requirements

### Requirement: Storage Analysis is a first-class workspace
Storage Analysis SHALL be reachable from the sidebar Storage Analysis destination, the Home Analyze Storage quick action, the command palette, and a Global default shortcut for `storage.analyze`. Opening it SHALL show the existing Storage Analysis surface over `contentRect` (Overview, Drill Down, Largest Folders, Largest Files, By Category, Treemap) rather than a modal utility dialog.

#### Scenario: Sidebar opens Storage Analysis
- **WHEN** the user activates the sidebar Storage Analysis destination
- **THEN** the Storage Analysis workspace is shown using the active pane path as the initial scope when that path is a filesystem location, or a volume-level scope when the location is Home

#### Scenario: Global shortcut opens Storage Analysis
- **WHEN** the user presses the default Global shortcut for `storage.analyze` (Ctrl+Shift+S)
- **THEN** the Storage Analysis workspace is shown and Fast Search is hidden if it was visible
