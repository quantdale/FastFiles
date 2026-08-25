## ADDED Requirements

### Requirement: UIA commands have semantic activation contracts
The UI automation driver SHALL report a command as successful only after it proves the expected result was uniquely available and selected, product input ownership was established, activation was accepted, the palette closed for the activation, and the command-specific postcondition was observed.

#### Scenario: Toggle Dual Pane is invoked from the palette
- **WHEN** the driver searches for and activates Toggle Dual Pane
- **THEN** it distinguishes command-not-found, ambiguous-result, wrong-selection, lost-input, unchanged-workspace, unchanged-ColumnView, stale-tree, and successful activation outcomes

### Requirement: Replaced UIA fragments are reacquired from the live root
After a product operation that may reconstruct a custom-rendered fragment, the driver SHALL discard traversal state rooted in the old fragment and reacquire the required elements from the current main-window provider.

#### Scenario: Pane.1 appears after a toggle
- **WHEN** the pre-toggle ColumnView fragment is invalidated
- **THEN** the driver discovers a fresh ColumnView and obtains Pane.0 and Pane.1 from that current subtree before reading geometry or selection

#### Scenario: A stale provider is reused
- **WHEN** an element reports UIA_E_ELEMENTNOTAVAILABLE or an identity mismatch
- **THEN** the driver records a stale-provider outcome and reacquires from the live root instead of retargeting by row or child index

### Requirement: Input actions have verified preconditions and postconditions
Every native input action used by a scenario SHALL have a bounded foreground/focus precondition and a semantic postcondition; polling cadence SHALL not itself count as proof.

#### Scenario: Pane activation by click
- **WHEN** the driver activates Pane.1
- **THEN** it proves the application owns the foreground and that the active-pane or keyboard-focus state changed to Pane.1 before sending pane-local keyboard input

### Requirement: Scenario process cleanup is ownership-scoped
The driver SHALL snapshot relevant processes before launch, validate executable identity and scenario ownership before termination, wait for actual exit, and report cleanup or pipe-lifecycle failures as first-class failures.

#### Scenario: An unrelated installation has the same executable name
- **WHEN** process cleanup runs
- **THEN** the driver leaves the unrelated process untouched and records the path and ownership reason used to exclude it

### Requirement: UIA failure diagnostics are structured
Driver failures SHALL include the action stage, semantic predicate, timeout classification, window and process identity where safe, provider identity observations, and bounded state snapshots rather than only a free-form timeout string.

#### Scenario: A pane postcondition times out
- **WHEN** Pane.1 is not discovered before the semantic deadline
- **THEN** the failure identifies the command, pre/post pane counts, active pane, foreground owner, popup state, provider identities, and whether the workspace state was observable
