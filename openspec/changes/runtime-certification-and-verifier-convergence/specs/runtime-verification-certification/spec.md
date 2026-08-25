## Purpose

This capability defines the authoritative, repeatable result and lifecycle contract for FastFiles' local PowerShell verification runs. It makes wrapper output, child scenarios, persisted evidence, reports, and process exit codes agree without weakening product security or converting unavailable external context into a pass.

## ADDED Requirements

### Requirement: Authoritative identity-bound result envelope

Every verification invocation SHALL create one authoritative result envelope bound to a unique invocation/run ID, and every executed scenario, provider phase, and cleanup operation SHALL identify that invocation. A capability and its scenarios SHALL record stable IDs, start/end UTC timestamps, status, machine-readable reason code, bounded diagnostic summary, evidence paths, provider identity where applicable, and cleanup outcome. The envelope SHALL distinguish `PASS`, `FAIL`, and `SKIPPED` from classifications such as `BLOCKED_ENVIRONMENT`, `REQUIRED-BUT-UNAVAILABLE`, `ASSERTION_FAILURE`, `HARNESS_ERROR`, `TIMEOUT`, `CANCELLED`, and `CLEANUP_INCOMPLETE`.

#### Scenario: A successful capability has one authoritative envelope

- **WHEN** a capability completes with all required assertions passing and only acceptable skips
- **THEN** exactly one envelope for that invocation SHALL be persisted, its child scenario IDs SHALL be unique and bound to the same run ID, and its status, summary, evidence, and exit code SHALL describe the same outcome

#### Scenario: A malformed or missing capability result is not a product pass

- **WHEN** a capability emits no result, more than one authoritative result, an invalid result object, an unknown status, or a result that fails schema validation
- **THEN** the harness SHALL produce an explicit `FAIL` envelope classified `HARNESS_ERROR`, preserve bounded diagnostics, and SHALL NOT infer PASS or SKIPPED from counters or console text

### Requirement: Deterministic result aggregation and error precedence

The top-level result SHALL be a pure deterministic projection of validated child states and capability-level lifecycle state. PASS SHALL require that every required executed assertion and contract check passed and that no lifecycle, schema, harness, timeout, cancellation, provider, or cleanup failure exists. An assertion failure SHALL remain visible when cleanup also fails; cleanup SHALL be recorded as a distinct failure and SHALL not erase the primary failure. A zero-failure child set SHALL not return a failure unless an explicit capability-level failure is represented in the envelope.

If a capability declares `FAIL` while emitting no attributable child or lifecycle failure, the harness SHALL represent that declaration as a capability-level `HARNESS_ERROR` with reason `capability-failed-without-attributed-cause`, retaining the raw declared reason as bounded diagnostic data. An explicitly classified capability-level failure MAY retain its declared classification and reason. The wrapper SHALL never expose an opaque `scenario-failed` string as the sole explanation for a zero-failure child set.

#### Scenario: PASS plus optional SKIP remains PASS

- **WHEN** executed scenarios pass and optional scenarios are skipped with a documented environment or provider reason
- **THEN** the capability SHALL be PASS with the skips preserved as child records and the supported wrapper SHALL return exit code 0

#### Scenario: Cleanup failure after PASS is a visible failure

- **WHEN** all product assertions pass but owned process, pipe, fixture, Explorer-window, provider, or report cleanup cannot be proven complete
- **THEN** the capability SHALL be FAIL with classification `CLEANUP_INCOMPLETE`, SHALL retain the passing child results and cleanup evidence, and SHALL return a nonzero exit code

#### Scenario: Assertion failure and cleanup failure preserve both causes

- **WHEN** a product assertion fails and teardown also reports residue
- **THEN** the envelope SHALL retain the assertion as the primary failure and cleanup as a separate attributed failure, with deterministic precedence and no opaque replacement by `scenario-failed`

#### Scenario: Unavailable context is not an assertion failure

- **WHEN** a capability or scenario cannot execute because a declared provider, elevation, interactive foreground, Explorer source, signer, clean host, guest, or physical display prerequisite is unavailable
- **THEN** it SHALL be recorded as `SKIPPED` with `BLOCKED_ENVIRONMENT` or `REQUIRED-BUT-UNAVAILABLE` classification and a stable reason code, and SHALL not be reported as a product PASS or an unexplained FAIL

### Requirement: Run and scenario lifecycle ownership

Each invocation SHALL own and identify its run directory, UI and engine processes, named pipes, temporary fixtures, Explorer windows opened by the scenario, logs, screenshots/tree dumps, provider lifecycle, and UIA provider lifetime. Cleanup SHALL use exact executable path plus PID and start-time identity where available, SHALL wait for actual termination or resource absence within a bound, and SHALL report residue. It SHALL never terminate unrelated same-name processes or consume another invocation's resources.

#### Scenario: A fresh invocation is isolated from an interrupted invocation

- **WHEN** the previous invocation was interrupted, cancelled, timed out, or left owned processes/resources behind
- **THEN** the next invocation SHALL either reclaim only positively identified owned residue or report a truthful prerequisite/cleanup failure, and SHALL not silently adopt stale UIA fragments, fixtures, pipes, logs, or processes

#### Scenario: Successful cleanup proves termination

- **WHEN** a scenario teardown is reported successful
- **THEN** the evidence SHALL show that every owned process identity exited, the owned engine pipe is absent or explicitly unverifiable, owned Explorer windows and fixtures are gone, and the UIA/provider lifetime was released

### Requirement: Run-tree identity and stale-artifact rejection

Every run SHALL write to a unique invocation-owned directory and persist its identity in the manifest and every result. Result discovery, report generation, and status composition SHALL read only the explicitly selected invocation and its declared envelope paths. A timestamp collision, duplicate result, stale previous-run artifact, path escape, missing artifact, or cross-run identity mismatch SHALL be a harness failure rather than a best-effort selection of the latest file.

#### Scenario: A stale previous-run result cannot satisfy a new run

- **WHEN** an earlier run contains a PASS result and the new invocation has no valid result for its own run ID
- **THEN** the new invocation SHALL fail as `HARNESS_ERROR` and SHALL not inherit the earlier PASS

#### Scenario: Reports agree with one run tree

- **WHEN** reports are generated or regenerated for an invocation
- **THEN** every report format and the index SHALL derive the same capability/scenario statuses from that invocation's validated envelopes, with no result discovered from another run

### Requirement: Bounded execution, timeout, and cancellation semantics

Every capability and scenario group SHALL have a declared bounded execution budget. If a provider, UIA/COM operation, process wait, or capability does not complete within its budget, the harness SHALL record a `FAIL` classified `TIMEOUT` (or `CANCELLED` for an explicit cancellation), collect bounded diagnostics, perform owned cleanup, and return a terminal result. The harness SHALL use semantic readiness, lifecycle synchronization, UIA state/events, and bounded condition waits; adding unbounded sleeps or retrying a whole scenario until it passes SHALL not satisfy this requirement.

#### Scenario: A hung UIA capability becomes diagnosable

- **WHEN** the supported UIA invocation exceeds its capability budget without returning a result
- **THEN** the wrapper SHALL terminate or isolate the owned execution, persist a timeout envelope naming the capability/run and cleanup result, and return the documented nonzero timeout/harness exit rather than hanging indefinitely or emitting no result

#### Scenario: Cancellation is preserved across cleanup

- **WHEN** an invocation is cancelled while a scenario or provider phase is executing
- **THEN** the result SHALL identify cancellation, finish owned cleanup within its bound, preserve partial evidence and the cancellation point, and SHALL not be converted into PASS by a later cleanup success

### Requirement: Deterministic UIA repeatability and order evidence

The supported UIA capability SHALL accept and persist a deterministic seed and scenario selection. Its scenario registry SHALL expose stable scenario IDs and lifecycle modes, and repeatability coverage SHALL exercise single scenarios, groups, the complete capability, isolated/mixed partitions, seeded order permutations, back-to-back runs, interrupted-run recovery, and supported Search, pane, degraded-engine, and Explorer-assisted transitions. Results SHALL retain the first failure and distinguish a real product assertion from an interactive or external prerequisite boundary.

#### Scenario: Identical seeded order is reproducible

- **WHEN** the same supported scenario selection is executed twice with the same seed and clean owned state
- **THEN** the scenario order, lifecycle identity contract, status/reason classifications, and required assertion set SHALL be equivalent, apart from timestamps and paths

#### Scenario: Order permutations cannot hide contamination

- **WHEN** the same scenario set is run in deterministic seeded permutations and mixed shared/isolated modes
- **THEN** any differing result SHALL retain the first divergent scenario and ownership/evidence data; the harness SHALL not retry the whole permutation until it becomes green

#### Scenario: Foreground or Explorer limitations remain truthful

- **WHEN** the current desktop cannot provide foreground ownership or a real Explorer drag source
- **THEN** the affected scenario SHALL record the stable external reason and SHALL not send downstream input or claim product coverage that was not exercised

### Requirement: Release-truth status composition

Project status and release-readiness reports SHALL mechanically distinguish locally implemented, locally validated, hosted-CI validated, interactive/environment blocked, elevation/privilege blocked, authorized-signing blocked, clean-host/guest blocked, and external administrative follow-up. Generated counts SHALL come from current Git, CTest, capability envelopes, release evidence, and OpenSpec task ledgers; hand-maintained summaries SHALL not override those sources. Unsigned release-test artifacts SHALL never be represented as production certification.

#### Scenario: A hosted result is tied to the pushed source

- **WHEN** hosted CI evidence is incorporated into a handoff
- **THEN** it SHALL identify the exact pushed commit SHA and workflow result, and a prior run or stale workstate value SHALL not be treated as the current hosted result

#### Scenario: Missing production prerequisites remain external

- **WHEN** authorized production signing material, pinned certificates, an elevated clean host, a supported guest, or a physical display matrix is unavailable
- **THEN** release readiness SHALL record the precise external blocker and SHALL not weaken security or claim production/privileged certification

### Requirement: Verifier regression coverage

The repository SHALL contain deterministic automated coverage for the result and lifecycle contract, including all-pass, pass-plus-skip, all-skip, assertion failure, harness exception, cleanup failure after pass and fail, environment/provider unavailable, malformed/multiple/missing output, stale artifacts, cancellation, timeout, unknown scenario/capability, repeated identical seeded execution, and fresh execution after interruption.

#### Scenario: The verifier core rejects contradictory fixtures

- **WHEN** a regression fixture presents any unsupported, missing, duplicate, stale, or contradictory result state
- **THEN** the verifier test SHALL fail the fixture with the expected reason/classification and SHALL prove that no green exit code is emitted
