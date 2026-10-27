## Purpose

Ensure the verification client observes genuine standard Windows controls, events, and source-thread capture without changing global accessibility configuration.

## ADDED Requirements

### Requirement: Installed standard client proxy initialization

The managed verification client SHALL initialize the installed standard UIA client proxy from the loaded UIAutomationClient directory before live UIA use. It SHALL first initialize the public UIA runtime with a RootElement read, then preserve installed version, token, culture and path and permit only a case adjustment of the known standard assembly name to match its exact exported entry namespace. It SHALL reject unrelated names and unavailable metadata, cache failures, produce no success output, and reuse successful process-local initialization across module reloads. Mock operations SHALL remain independent of native assembly initialization.

#### Scenario: First managed operation in a fresh client

- **WHEN** no AutomationElement query has yet initialized the runtime
- **THEN** the driver initializes through the public root accessor before proxy registration
- **AND** marks initialization successful only after registration succeeds

#### Scenario: Namespace casing differs from installed assembly spelling

- **WHEN** the installed standard assembly exports its entry class under a namespace differing only in case from the known standard assembly name
- **THEN** the client registers that namespace-matching identity using the public API without mutating the original identity or installed files
- **AND** subsequent initialization reuses the successful process-local record

#### Scenario: Metadata or registration is unavailable

- **WHEN** the installed standard provider metadata is invalid or registration throws
- **THEN** initialization fails with a clear cached reason and does not mark the managed API initialized
- **AND** mock operations remain usable without native initialization

### Requirement: Source-thread capture observation

The capture observer SHALL pass the thread ID returned by GetWindowThreadProcessId to GetGUIThreadInfo, return false for an invalid handle or failed lookup, and report any nonzero capture window on the owning thread without changing capture.

#### Scenario: Own child window holds source-thread capture

- **WHEN** the fixture's own STA thread normally captures one of its child windows
- **THEN** querying either the parent or another child on that thread observes capture
- **AND** querying after normal release or with a destroyed/zero handle returns false

### Requirement: Explicit native diagnostics use real controls and events

The explicit native diagnostic SHALL create only its own raw standard checkbox and dropdown-list controls, bind their HWND/PID/classes, assert genuine UIA roles/pattern availability and a genuine title Name event, and close normally. It SHALL remain outside default portable CTest and SHALL NOT invoke FastFiles, foreign capture, global configuration, or process termination.

#### Scenario: Own native diagnostic succeeds

- **WHEN** the explicit diagnostic runs in an interactive Windows session
- **THEN** actual public UIA exposes CheckBox/Toggle and ComboBox/Selection/ExpandCollapse plus a genuine title Name event
- **AND** normal WM_CLOSE and bounded thread join leave no fixture window
