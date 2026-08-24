## Purpose

Exercise the repository's existing build, test, policy, provider, and release-candidate machinery in bounded hosted CI without weakening production signing or claiming external validation.

## ADDED Requirements

### Requirement: Bounded qualification gates

Hosted CI SHALL expose diagnosable gates for strict OpenSpec validation, source and diagnostic policy, status drift, Debug and release-test CTest, Analyze, ASAN protocol/IPC fuzzing, portable trust tests, provider contract tests, and unsigned release-candidate packaging.

#### Scenario: One gate fails with actionable evidence

- **WHEN** a qualification command fails
- **THEN** the job reports a stable gate name, exit status, bounded relevant diagnostics, and safe structured artifacts without uploading personal desktop data or signing material

### Requirement: Non-production candidate classification

The release-candidate path SHALL classify an unsigned candidate explicitly as non-production, emit deterministic names/checksums/provenance/dependency/release-note metadata, and fail on version mismatch or malformed production signing inputs.

#### Scenario: Unsigned preparation never becomes production

- **WHEN** a candidate is built without authorized production signer and pin inputs
- **THEN** it may pass unsigned preparation only with a non-production classification, while production readiness remains blocked or failed closed

#### Scenario: Reproducibility is checked

- **WHEN** the same candidate metadata is generated twice from the same source identity
- **THEN** the safe deterministic fields and checksums match, and cleanup removes only the run-owned staging directory

### Requirement: Release-readiness frontier report

The qualification pipeline SHALL emit one structured report and Markdown projection that groups results into locally certified, environment-blocked, provider-blocked, production-signing-blocked, and clean-host/elevated-blocked categories.

#### Scenario: External requirements remain open

- **WHEN** no authorized signer or usable disposable Windows guest is available
- **THEN** the report records the exact missing prerequisite and next command while leaving the corresponding requirement non-certified
