## ADDED Requirements

### Requirement: Lifecycle metadata is explicit and validated

The repository SHALL provide a schema-versioned lifecycle manifest for every
OpenSpec change that has an unchecked task, and the status collector SHALL
validate change names, lifecycle states, supersession targets, task IDs,
categories, dispositions, statuses, and evidence fields before projecting the
frontier.

#### Scenario: Valid active change

- **WHEN** an active change has an unchecked local task and a valid manifest
  entry
- **THEN** the task is retained in the raw OpenSpec inventory and appears in
  `frontier.localOpen`

#### Scenario: Missing or malformed lifecycle metadata

- **WHEN** an unchecked change has no valid lifecycle entry, or the manifest is
  malformed or references an unknown change
- **THEN** status generation reports a model failure, retains the raw task under
  an ambiguity bucket, and does not add it to `frontier.localOpen`

### Requirement: Historical and superseded work is not current local work

The status model SHALL preserve unchecked tasks from completed, historical, or
superseded changes with lifecycle state, disposition, source, and evidence, but
SHALL exclude them from `frontier.localOpen`.

#### Scenario: Completed change with external evidence pending

- **WHEN** a completed change retains an unchecked external signing, provider,
  or clean-host task
- **THEN** the task remains visible in the appropriate blocked bucket with
  historical lifecycle provenance and is not counted as local implementation
  work

#### Scenario: Superseded change

- **WHEN** a change is marked superseded and names a newer change
- **THEN** its unchecked tasks remain visible under superseded provenance and
  the newer change is recorded as the supersession target

### Requirement: Explicit task metadata controls classification

Task-level category, disposition, status, and evidence metadata SHALL override
prose-only keyword inference. The fallback classifier SHALL identify itself as
low-confidence when it infers a local task.

#### Scenario: Misleading task prose

- **WHEN** a current local task mentions signing or an interactive surface only
  as context but declares an explicit local disposition
- **THEN** it remains local-open with an explicit classification source rather
  than being promoted to a signing or interactive blocker

#### Scenario: Mixed task

- **WHEN** a task contains both local and external clauses
- **THEN** it is represented as mixed or split metadata and does not silently
  flatten into an incorrect single local state

### Requirement: Release readiness uses the lifecycle-aware frontier

The release-readiness projection SHALL embed the same lifecycle summary and
frontier buckets as project status, SHALL fail on lifecycle model errors, and
SHALL retain external prerequisites regardless of historical filtering.

#### Scenario: Historical filtering with unsigned candidate

- **WHEN** historical local tasks are excluded from `localOpen` while the
  candidate remains unsigned or an external prerequisite is unavailable
- **THEN** release readiness SHALL not report production readiness

### Requirement: Projections are deterministic and redacted

Repeated generation over the same repository state SHALL produce stable task
ordering and SHALL redact host-private paths from JSON and Markdown projections.

#### Scenario: Repeat generation

- **WHEN** project status is generated twice for the same tree
- **THEN** task ordering and lifecycle bucket membership are identical and
  host-private paths are absent from both projections
