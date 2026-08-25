## ADDED Requirements

### Requirement: Dual-pane selection remains pane-local through lifecycle changes
Selection state SHALL remain associated with the logical pane and item identity that owns it while a second pane is created, activated, navigated, or removed.

#### Scenario: Selection is made in Pane.0 before enabling dual pane
- **WHEN** dual pane is enabled and Pane.1 is navigated independently
- **THEN** Pane.0 retains its selection and Pane.1 starts with only the selection produced by its own actions

#### Scenario: Pane.1 is removed while active
- **WHEN** dual pane is disabled while Pane.1 owns focus
- **THEN** Pane.0 receives focus and its selection identity remains valid without a stale Pane.1 selection provider

### Requirement: UIA selection actions target live pane identity
Selection and SelectionItem operations SHALL resolve against the current pane identity and SHALL fail safely when a provider has been replaced.

#### Scenario: A stale selection item is used after a toggle
- **WHEN** a selection item was obtained before pane reconstruction
- **THEN** the operation reports the element unavailable or reacquires the item from the current pane, and never applies the action to another pane
