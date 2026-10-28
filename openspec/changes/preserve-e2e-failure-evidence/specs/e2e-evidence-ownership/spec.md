## Purpose
Associate black-box E2E evidence with the scenario invocation that produced it, retaining useful inner observations and separately documenting outer failures.

## ADDED Requirements

### Requirement: E2E artifact paths belong to the current run
The E2E runner SHALL accept a new or empty artifact root and SHALL refuse a populated or non-directory root without overwriting existing files. A scenario SHALL reserve a fresh directory before executing its body and SHALL fail without running when its directory already exists.

#### Scenario: Reusing a populated root
- **WHEN** an E2E run is requested using an artifact root containing a previous summary
- **THEN** the run SHALL fail before executing UIA work and SHALL preserve the previous summary

#### Scenario: Existing empty root
- **WHEN** the selected artifact root already exists but is empty
- **THEN** the runner SHALL permit normal provenance preflight and scenario execution

#### Scenario: Scenario path already occupied
- **WHEN** a scenario's intended artifact directory or file already exists
- **THEN** the scenario body SHALL NOT run and the old path SHALL remain unchanged

### Requirement: Inner failure observations survive the wrapper
When a scenario body captures a failure envelope during its own invocation, the outer wrapper SHALL preserve that envelope and adjacent captured tree and screenshot. Returned failures and exceptions SHALL retain their terminal failure status and SHALL record outer context separately without overwriting inner files.

#### Scenario: Cold restore captured a live failure
- **WHEN** a cold-restore scenario captures an inner failure envelope and then returns failure without an outer live context
- **THEN** the inner envelope, screenshot and UIA tree SHALL remain unchanged and outer context SHALL be recorded separately

#### Scenario: Exception after capture
- **WHEN** a scenario captures inner evidence and then throws
- **THEN** the inner evidence SHALL remain unchanged and the exception SHALL be recorded in a separate outer envelope

#### Scenario: No inner evidence exists
- **WHEN** a scenario fails before capturing any inner evidence
- **THEN** the outer wrapper SHALL create a failure envelope explaining its own observation limits

### Requirement: Scenario success requires an explicit valid result
A scenario wrapper SHALL accept exactly one result with the expected scenario id, required result fields, a non-empty stage, finite nonnegative numeric duration and a recognized PASS, FAIL or SKIPPED status. Missing, malformed, mismatched or multiple output SHALL become a failure and SHALL receive a diagnostic envelope, preserving any previously captured inner evidence.

#### Scenario: Scenario returns no result
- **WHEN** a scenario body returns no result
- **THEN** the wrapper SHALL report `FAIL` at `invalid-result` and SHALL record the validation failure

#### Scenario: Unknown status or mismatched scenario
- **WHEN** a scenario returns an unsupported status or an id belonging to another scenario
- **THEN** the wrapper SHALL report `FAIL` and SHALL NOT count the result as a pass

#### Scenario: Explicit skip
- **WHEN** a scenario returns a well-formed `SKIPPED` result
- **THEN** the wrapper SHALL retain its skipped status and reason
