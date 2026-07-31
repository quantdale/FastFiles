## ADDED Requirements

### Requirement: UI Automation Via UIA, Not Pixel Comparison
The harness SHALL provide a UI Automation capability that drives and inspects `FastFiles` through the Windows UI Automation (UIA) provider tree — control identity, patterns, properties, and raised events — and SHALL NOT rely on whole-screen pixel comparison; where a property cannot be expressed through UIA, any visual assertion SHALL be tolerant and scoped to a specific element region rather than a full-frame pixel diff. This capability is Tier-2-gated and SHALL report `SKIPPED(context-absent)` when an interactive UIA context is unavailable.

#### Scenario: Verification uses the UIA tree, not pixels
- **WHEN** the UI Automation capability verifies UI state
- **THEN** it SHALL assert against UIA element identity, patterns, properties, and events, and SHALL NOT depend on full-screen pixel matching

#### Scenario: Missing UIA provider skips affected checks with a reason
- **WHEN** the custom Direct2D/DirectComposition surface does not expose a UIA provider for an element under test
- **THEN** the affected checks SHALL report `SKIPPED` with a precise reason rather than fall back to brittle pixel comparison

#### Scenario: No interactive context skips the capability
- **WHEN** no interactive UIA-capable session is available (Tier-2 context absent)
- **THEN** the UI Automation capability SHALL report `SKIPPED` with a machine-readable reason and required-context descriptor, and SHALL NOT be reported as passed or failed

### Requirement: UI Launch, Navigation And Interaction
When an interactive UIA context is available, the harness SHALL launch `FastFiles`, navigate directories through the multi-column interface, perform selection, and drive both keyboard and mouse interactions through UIA/`SendInput`, verifying the resulting UI state.

#### Scenario: Launch and connect
- **WHEN** the capability launches `FastFiles`
- **THEN** it SHALL confirm the main window appears in the UIA tree and the application reaches a ready state, recording the outcome

#### Scenario: Multi-column navigation via keyboard and mouse
- **WHEN** the capability navigates into a folder using keyboard arrows/Enter and, separately, mouse selection
- **THEN** it SHALL confirm a new column is populated to the right for a folder selection and that the selected item is reflected in the UIA tree, for both input methods

#### Scenario: Scrolling is verified through UIA
- **WHEN** total column width or list length exceeds the viewport and the capability scrolls
- **THEN** it SHALL confirm the scroll state changes as expected via UIA scroll patterns/properties

### Requirement: UI State And Error-Surface Verification
When an interactive UIA context is available, the harness SHALL verify UI state surfaces: the engine-connection badge, dialogs, search, and in-column error states (permission-denied, no-longer-available), and SHALL verify rendering where practical through UIA-exposed geometry/state.

#### Scenario: Connection badge reflects engine state
- **WHEN** the engine connection state changes (e.g. degraded vs. instant)
- **THEN** the capability SHALL confirm the connection badge in the UIA tree reflects the corresponding state

#### Scenario: Error states are surfaced, not hidden
- **WHEN** a column encounters a permission-denied or no-longer-available condition
- **THEN** the capability SHALL confirm the corresponding error message is present in the UIA tree and the UI remains responsive

#### Scenario: Dialogs and search are verified
- **WHEN** a dialog is invoked or a search is performed
- **THEN** the capability SHALL confirm the dialog/search UI appears and behaves per design through UIA, recording the outcome

#### Scenario: Rendering is checked where practical
- **WHEN** a rendering property is exposed through UIA geometry/state (e.g. element bounds, visibility)
- **THEN** the capability SHALL assert on it via UIA, resorting to a tolerant element-scoped visual check only where UIA cannot express the property
