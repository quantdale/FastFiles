## ADDED Requirements

### Requirement: Engine executable signature verification
Before launching any engine executable, the system SHALL verify its Authenticode signature thumbprint against the pinned allowlist. If verification fails, the launcher SHALL NOT execute the binary and SHALL log the failure with the candidate path and thumbprint.

#### Scenario: Valid engine signature
- **WHEN** the engine discovery scans the install directory and finds FastFilesEngine.exe with a thumbprint matching the pinned allowlist
- **THEN** the launcher proceeds to create the process and establishes the named-pipe connection

#### Scenario: Invalid engine signature
- **WHEN** the engine discovery finds FastFilesEngine_evil.exe with a thumbprint that does not match the pinned allowlist
- **THEN** the launcher rejects the candidate, logs the mismatch, and continues scanning or falls back to the scheduled-task activation path

#### Scenario: Missing signature
- **WHEN** the candidate executable has no Authenticode signature (e.g., unsigned debug build on a clean machine)
- **THEN** the launcher rejects the candidate unless a development-mode override is active
