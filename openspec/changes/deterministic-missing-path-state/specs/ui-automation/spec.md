# deterministic-missing-path-state — ui-automation delta

## Purpose

This delta makes the existing semantic Column View status provider and live UIA validation deterministic for directories that disappear after being opened. It does not add a new accessibility tree; it requires the existing provider to reflect the real visible missing-path state and removes the race-dependent live SKIP once the product state is deterministic.

## ADDED Requirements

### Requirement: Missing-path status is semantically identical to the visible status
When a Column View path is in the confirmed no-longer-available state, the existing column status UIA element SHALL expose the same user-visible wording and geometry as the rendered status. It SHALL not expose stale file rows as current semantic children of an unavailable column.

#### Scenario: UIA observes a deleted current directory
- **WHEN** an open directory is externally deleted and the engine publishes `NotFound`
- **THEN** the column's semantic status element reports `This folder is no longer available.`, prior row elements are no longer current children, and the provider remains responsive

### Requirement: Missing-path transitions preserve stale-provider safety
Semantic item providers obtained before a confirmed path disappearance SHALL fail cleanly when their backing item no longer exists and SHALL never retarget rows created after the directory is recreated.

#### Scenario: Held item across delete and recreate
- **GIVEN** a UIA client holds a pre-delete row provider
- **WHEN** the directory becomes unavailable and is then recreated with different contents
- **THEN** the held provider fails under the existing stale-element contract and cannot mutate the recreated row

### Requirement: Live missing-path validation is deterministic
The `windows-ui-automation-validation` capability SHALL exercise the engine-connected missing-path transition through the normal unprivileged product path. The scenario SHALL use a disposable real directory, navigate through the real UI, delete it externally, wait semantically for the no-longer-available state, then recreate it and verify normal recovery.

#### Scenario: Late in-column error state no longer depends on a refresh race
- **WHEN** `in-column-error-state-late` runs in a fresh isolated lifecycle
- **THEN** it reaches the missing-path status deterministically without `error-state-trigger-not-deterministic`, pixel/OCR fallback, private product hooks, or retry-until-pass loops

#### Scenario: Repeated execution is stable
- **WHEN** the affected live scenario is run at least ten consecutive times
- **THEN** all runs reach the same semantic missing-path and recovery states with zero unexpected FAIL

### Requirement: Missing-path automation requires no privileged service
The deterministic live validation SHALL run with the ordinary per-user engine/degraded enumeration path and SHALL NOT require FastFilesIndexSvc, administrator elevation, production signing, trust-store mutation, or real MFT/USN access.

#### Scenario: Privileged service is absent
- **WHEN** the privileged service is unavailable or unverified in a development tree
- **THEN** the missing-path live scenario remains executable and its result is not SKIPPED for that reason
