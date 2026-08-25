## ADDED Requirements

### Requirement: Lifecycle evidence is typed and resolvable

The lifecycle manifest SHALL use schema-versioned evidence objects. Repository
paths SHALL resolve inside the repository without traversal or reparse-point
ambiguity; Git evidence SHALL use a full reachable commit SHA; hosted-run
evidence SHALL bind a run ID and exact SHA to a tracked repository record; and a
note SHALL be marked non-authoritative.

#### Scenario: Valid repository and Git evidence

- **WHEN** a manifest references an existing repository artifact and a full
  commit ancestor of `HEAD`
- **THEN** the lifecycle model records normalized typed evidence as
  authoritative and status generation remains deterministic and offline

#### Scenario: Invalid or legacy evidence

- **WHEN** evidence is a schema-v1 string, missing path, traversal/absolute
  path, reparse-point path, abbreviated/nonexistent/unreachable SHA, or an
  unbound hosted run
- **THEN** lifecycle integrity fails with a reason code, raw tasks remain
  visible, and the evidence cannot remove work from the active frontier

### Requirement: Lifecycle semantics are internally consistent

The model SHALL reject self-supersession, supersession cycles, invalid targets,
authoritative state transitions without authoritative evidence, contradictory
task disposition/category/status combinations, and unchecked PASS overrides
without sufficient proof.

#### Scenario: Supersession graph

- **WHEN** a superseded change points to itself, an ambiguous/superseded target,
  or a cycle of changes
- **THEN** status reports a semantic lifecycle error and preserves the affected
  tasks under supersession or ambiguity provenance

#### Scenario: Mixed and external task metadata

- **WHEN** an external override has only a local category, or a mixed override
  lacks either a local or external category
- **THEN** the semantic model fails closed and the task is not projected as
  local-open

### Requirement: Consumers share integrity truth

Project-status and release-readiness SHALL consume the same validated lifecycle
model. They SHALL expose structural, evidence, and semantic error classes,
typed evidence authority, raw unchecked counts, and the same frontier buckets.

#### Scenario: Integrity failure and release composition

- **WHEN** lifecycle evidence or semantics fail validation while a release
  report otherwise appears successful
- **THEN** raw tasks remain visible in ambiguity, `frontier.localOpen` is not
  populated from invalid metadata, and release readiness is not production
  ready

### Requirement: Evidence integrity is regression-tested offline

The repository SHALL provide deterministic tests for valid and invalid evidence,
path safety, Git reachability, hosted-run binding, supersession cycles,
contradictory overrides, stable projections, safe redaction, schema
synchronization, and release fail-closed behavior without network, elevation,
signing material, or an interactive desktop.

#### Scenario: Regression fixtures remain offline and deterministic

- **WHEN** the focused project-status and release-readiness tests run against
  their temporary fixtures
- **THEN** they exercise valid and invalid evidence plus semantic failures
  without network, elevation, signing material, or an interactive desktop, and
  repeated frontier generation produces the same ordered projection
