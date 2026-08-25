## ADDED Requirements

### Requirement: Storage controls retain truthful UI Automation semantics

Owner-drawn Storage presentation SHALL remain backed by real actionable native
controls. UI Automation SHALL expose the mode controls with their correct names,
radio control type, enabled/focused/selected state, and selection behavior;
custom painting SHALL not create a parallel accessibility state model.

#### Scenario: UIA activates a Storage mode
- **WHEN** a UI Automation client invokes or selects a visible Storage mode control
- **THEN** the real Storage mode mutation SHALL run, exactly one mode SHALL become selected, and the client SHALL observe the updated state

#### Scenario: Stale Storage control fails safely
- **WHEN** a UI Automation client holds an element after the Storage surface is hidden or destroyed
- **THEN** the stale element SHALL fail with the existing unavailable-element contract and SHALL not retarget a replacement control

#### Scenario: Existing dynamic pane provider contracts remain intact
- **WHEN** this campaign changes navigation presentation or Storage child controls
- **THEN** current Pane.0/Pane.1 replacement, fragment-generation, RuntimeId/AutomationId, selection isolation, and foreground-skip semantics SHALL remain unchanged
