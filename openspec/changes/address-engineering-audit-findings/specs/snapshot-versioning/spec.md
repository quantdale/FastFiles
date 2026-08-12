## ADDED Requirements

### Requirement: Snapshot format version
The shared-memory snapshot header SHALL include a format version field. The UI SHALL reject any snapshot whose version is incompatible with its expected version and SHALL trigger a re-mapping of the section.

#### Scenario: Mismatched snapshot version
- **WHEN** the UI maps the shared memory section and reads a snapshot version that it does not support
- **THEN** the UI unmaps the section, waits for a new generation notification, and re-maps

#### Scenario: Compatpatible snapshot version
- **WHEN** the UI maps the shared memory section and reads a snapshot version that it supports
- **THEN** the UI proceeds to render the snapshot data normally
