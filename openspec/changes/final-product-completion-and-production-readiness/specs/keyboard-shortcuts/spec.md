## ADDED Requirements

### Requirement: Default shortcut for Storage Analysis
`storage.analyze` SHALL ship a Global default shortcut of Ctrl+Shift+S. The existing Global Ctrl+F binding for `search.focus` SHALL remain. Users MAY rebind either command through the existing shortcut customization surface.

#### Scenario: Ctrl+Shift+S opens Storage Analysis on first run
- **WHEN** FastFiles runs with no persisted shortcut customization
- **THEN** Ctrl+Shift+S invokes `storage.analyze`

#### Scenario: Ctrl+F still focuses search
- **WHEN** FastFiles runs with no persisted shortcut customization
- **THEN** Ctrl+F invokes `search.focus`
