## ADDED Requirements

### Requirement: Driver-Backed End-To-End UI Automation Validation
When an interactive UIA context is available, the UI Automation capability SHALL be driven by the `uia-driver` capability to launch `FastFiles`, perform multi-column navigation (keyboard arrows/Enter and, separately, mouse selection), perform selection, drive scrolling, and verify the connection badge, dialogs, search, in-column error states (permission-denied, no-longer-available), and rendering-where-practical through UIA-exposed geometry/state — collecting UIA trees, application logs, event traces, screenshots, and resulting filesystem state as evidence, and failing clearly with a diagnostic tree dump when a semantic UIA target cannot be found.

#### Scenario: Launch and navigate via the driver
- **WHEN** the capability launches `FastFiles` and navigates into a folder using keyboard and mouse
- **THEN** it SHALL confirm via the UIA tree that a new column is populated and the selected item is reflected, for both input methods, and SHALL record the outcome

#### Scenario: Error states are surfaced through UIA, not hidden
- **WHEN** a column encounters a permission-denied or no-longer-available condition
- **THEN** the capability SHALL confirm the corresponding error message is present in the UIA tree and that the UI remains responsive

### Requirement: Cross-Window And Cross-Process Drag-And-Drop Validation
When an interactive UIA context is available, the UI Automation capability SHALL validate cross-window and cross-process drag-and-drop — dragging out of `FastFiles` into a real Explorer window (both copy and move effects), dragging from a real Explorer window into `FastFiles`, and dragging between two `FastFiles` windows/panes — driving the drop through UIA drag patterns where exposed and a `SendInput` mouse sequence otherwise, and SHALL verify the resulting filesystem side effects programmatically (files appear/disappear at source and destination).

#### Scenario: Drag out of FastFiles into Explorer
- **WHEN** the capability drags an item out of `FastFiles` into a real Explorer window
- **THEN** it SHALL verify the item appears at the Explorer destination and (for a move) is removed from the `FastFiles` source, programmatically

#### Scenario: Drag from Explorer into FastFiles
- **WHEN** the capability drags an item from a real Explorer window into `FastFiles`
- **THEN** it SHALL verify the item appears in the `FastFiles` destination column and is removed from the Explorer source (for a move), programmatically

#### Scenario: Drag between two FastFiles windows or panes
- **WHEN** the capability drags an item between two `FastFiles` windows or panes
- **THEN** it SHALL verify the item appears at the destination and is removed from the source (for a move), programmatically