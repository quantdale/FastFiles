## ADDED Requirements

### Requirement: Authenticode pin provisioning
Release builds SHALL NOT ship with placeholder (all-zero) Authenticode pins. The build system SHALL inject real pins or fail the build with a clear error.

#### Scenario: Placeholder pins detected at build time
- **WHEN** the CI build detects all-zero pins in the release configuration
- **THEN** the build fails with an error message directing the developer to provision real pins

#### Scenario: Development mode bypass
- **WHEN** a debug build runs with the development-mode flag set
- **THEN** signature verification is bypassed with a prominent console warning
