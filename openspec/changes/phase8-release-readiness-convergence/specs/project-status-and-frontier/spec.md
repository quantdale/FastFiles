## Purpose

Provide one machine-derived, auditable view of repository health and remaining release work so agents and CI cannot confuse stale prose or unavailable external prerequisites with certification.

## ADDED Requirements

### Requirement: Structured project status

The status command SHALL emit a schema-versioned JSON document containing the current branch and commit, clean/dirty state, environment fingerprint, discovered CTest count, latest available test result, capability availability, OpenSpec frontier, release-candidate classification, and generated timestamp.

#### Scenario: Status derives repository truth

- **WHEN** the status command runs in a Git worktree
- **THEN** its branch, SHA, dirty state, and diff-check result come from Git at execution time and are not copied from maintained prose

#### Scenario: Missing evidence remains distinct

- **WHEN** a test result, provider, signer, or host prerequisite cannot be observed
- **THEN** the JSON records `SKIPPED`, `BLOCKED_ENVIRONMENT`, or `REQUIRED-BUT-UNAVAILABLE` with a reason and never converts it to `PASS`

### Requirement: OpenSpec frontier classification

The status document SHALL enumerate every change with unchecked tasks, count those tasks, and classify each task using stable categories for local, interactive, elevated, external-provider, production-signing, and clean-host requirements. Classification SHALL preserve the task's open state and source location.

#### Scenario: Open tasks are complete and actionable

- **WHEN** an OpenSpec task ledger contains checked and unchecked tasks
- **THEN** the report includes exact change/task identifiers, unchecked counts, categories, and a reproducible next action for each unchecked task

#### Scenario: Stale project counts fail hygiene

- **WHEN** maintained status prose asserts a discovered test count or frontier count that differs from generated status
- **THEN** the status-drift test fails with the mismatched field and regeneration command

### Requirement: Sanitized Markdown projection

The status tooling SHALL project the structured JSON into Markdown without emitting credentials, signing material, personal paths, raw interactive screenshots, or machine-specific secrets. The projection SHALL separate locally certified, environment-blocked, provider-blocked, production-signing-blocked, and clean-host/elevated-blocked work.

#### Scenario: Release-readiness handoff is self-contained

- **WHEN** the status projection is generated
- **THEN** each blocked item includes its exact task, prerequisite, existing evidence, next command, and the evidence boundary that must not be inferred
