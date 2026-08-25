## ADDED Requirements

### Requirement: Palette selection is uniquely provable
The command palette automation surface SHALL make it possible to determine that an expected command result exists, is the selected result, is enabled, and is not ambiguous before activation.

#### Scenario: One matching command is selected
- **WHEN** the query produces exactly one enabled Toggle Dual Pane result
- **THEN** the driver can identify that result and its command identity before activation

#### Scenario: Multiple results match
- **WHEN** a query produces more than one candidate matching the requested command
- **THEN** activation fails as an ambiguity rather than guessing based on list position

### Requirement: Palette activation has an observable completion boundary
The command palette SHALL expose enough observable state for automation to distinguish accepted command activation from text entry, lost Enter input, and ordinary dismissal.

#### Scenario: Enter activates a command
- **WHEN** the selected result is activated
- **THEN** the palette closes and the caller can observe the command-specific product postcondition without sending duplicate activation input

#### Scenario: Escape dismisses the palette
- **WHEN** Escape is used to dismiss the palette
- **THEN** the dismissal is not reported as command activation and the requested product postcondition remains unclaimed
