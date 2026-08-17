# deterministic-missing-path-state — ui-automation delta

## Purpose

This delta makes the existing semantic Column View status provider and live UIA validation deterministic for directories that disappear after being opened. It does not add a new accessibility tree; it requires the existing provider to reflect the real visible missing-path state and removes the race-dependent live SKIP once the product state is deterministic.

## ADDED Requirements

### Requirement: Missing-path status is semantically identical to the visible status
When a Column View path is in the confirmed no-longer-available state, the existing column status UIA element SHALL expose the exact visible wording `This folder is no longer available.`, `ControlType=Text`, and a non-zero BoundingRectangle that belongs to the affected column. It SHALL not expose stale file rows as current semantic children of an unavailable column.

Broad error states such as `Showing previous results`, `refresh failed`, generic `unable to load`, `engine is unavailable`, or `This folder is empty` SHALL NOT satisfy the confirmed-missing assertion.

#### Scenario: UIA observes a deleted current directory
- **WHEN** an open, non-empty directory is externally deleted and the engine publishes `NotFound`
- **THEN** the column's semantic status element reports exactly `This folder is no longer available.` as a Text element with usable geometry, prior row elements are no longer current children, and the provider remains responsive

#### Scenario: Missing is not empty
- **WHEN** the deleted directory transition is being validated
- **THEN** `This folder is empty` is treated as a failure because it denotes `Success(0)`, not `NotFound`

### Requirement: Missing-path transitions preserve stale-provider safety
Semantic item providers obtained before a confirmed path disappearance SHALL fail cleanly when their backing item no longer exists and SHALL never retarget rows created after the directory is recreated.

#### Scenario: Held item across delete and recreate
- **GIVEN** a UIA client holds the provider for a pre-delete sentinel row
- **WHEN** the directory becomes unavailable and is then recreated with different contents
- **THEN** the held provider fails under the existing stale-element contract (`UIA_E_ELEMENTNOTAVAILABLE` or the driver's exact equivalent), the old sentinel is absent from the current tree, and the held provider cannot mutate or identify the recreated row

### Requirement: Live missing-path validation is deterministic
The `windows-ui-automation-validation` capability SHALL exercise the engine-connected missing-path transition through the normal unprivileged product path. The isolated scenario SHALL launch its own repo-built FastFiles UI/engine lifecycle, create a disposable real directory, navigate through the real UI, retain a sentinel row provider, externally delete the currently displayed directory, wait semantically for the exact no-longer-available state, then recreate the same path and verify normal recovery.

The scenario SHALL prefer observing the current-view watcher transition directly. It SHALL NOT depend on a navigation/history timing race as the correctness mechanism.

#### Scenario: Late in-column error state no longer depends on a refresh race
- **WHEN** `in-column-error-state-late` runs in a fresh isolated lifecycle and the UIA availability probe reports an interactive context
- **THEN** it reaches the exact missing-path status deterministically without `error-state-trigger-not-deterministic`, pixel/OCR fallback, private product hooks, or retry-until-pass loops

#### Scenario: Recovery is semantically current
- **WHEN** the same path is recreated with a different sentinel and a normal supported re-request/navigation is performed
- **THEN** the new sentinel is exposed as current, the old sentinel is absent, the missing status disappears, and the pre-delete provider remains stale

#### Scenario: Repeated execution is stable
- **WHEN** the affected live scenario is run at least ten consecutive times
- **THEN** all runs reach the same semantic missing-path and recovery states with zero unexpected FAIL and zero `error-state-trigger-not-deterministic` outcomes

### Requirement: UIA lifecycle availability is classified correctly
A pre-existing FastFiles desktop process SHALL NOT be required. The validation capability owns launch and teardown. If its availability probe reports an interactive Windows/UIA context, the missing-path scenario SHALL execute.

Only a real availability failure such as session 0/services, a non-interactive user context, or unavailable UIA driver MAY block the live scenario, and that condition SHALL be reported as `BLOCKED_ENVIRONMENT` with the exact probe evidence.

#### Scenario: No app is running before validation
- **WHEN** no FastFiles UI process is running but the UIA capability is available
- **THEN** the isolated scenario launches the repo-built app and executes normally rather than skipping

### Requirement: Missing-path automation requires no privileged service
The deterministic live validation SHALL run with the ordinary per-user engine/degraded enumeration path and SHALL NOT require FastFilesIndexSvc, administrator elevation, production signing, trust-store mutation, or real MFT/USN access.

#### Scenario: Privileged service is absent
- **WHEN** the privileged service is unavailable or unverified in a development tree
- **THEN** the missing-path live scenario remains executable and its result is not SKIPPED for that reason
