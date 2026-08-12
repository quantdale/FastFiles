## ADDED Requirements

### Requirement: Structured diagnostic codes
All diagnostics emitted by the system SHALL include a structured error code in addition to the human-readable message. The error code SHALL be an integer enum value defined in ffprotocol/Diagnostics.h.

#### Scenario: Diagnostic with code and message
- **WHEN** the engine fails to open the SQLite store due to corruption
- **THEN** the diagnostic event contains both the human-readable message and the structured error code FFDIAG_CORRUPT_DB

#### Scenario: Diagnostic consumed by automation
- **WHEN** a monitoring tool parses the diagnostic stream
- **THEN** the tool can filter and alert on specific error codes without parsing free-form text
