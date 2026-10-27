## ADDED Requirements

### Requirement: Shortcut creation preserves existing destinations
The worker SHALL create shortcut destinations exclusively, preserve pre-existing bytes and report failures without false completion.

#### Scenario: Destination appears after planning
- **WHEN** a destination is created after BuildLinkPlan and before worker execution
- **THEN** its bytes remain unchanged and the shortcut is reported as failed
- **AND** a manual retry may create a valid distinct shortcut

### Requirement: Failed writes clean only owned new files
The writer SHALL remove only its newly created file on failure and retain cleanup failures for reporting.

#### Scenario: Stream fails after writing bytes
- **WHEN** serialization output cannot be completely read
- **THEN** the new partial file is removed through its owned handle
- **AND** an existing destination is never deleted

### Requirement: Shortcut cancellation is cooperative and truthful
The worker SHALL check cancellation between shortcuts, report progress and issue one terminal event reflecting actual completion.

#### Scenario: Cancel an outstanding batch
- **WHEN** cancellation is accepted while shortcut work is outstanding
- **THEN** no additional items begin after it is observed
- **AND** successful shortcuts remain valid and completed counts match actual files
