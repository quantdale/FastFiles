## Purpose

Defines cross-lifecycle reliability guarantees for the already implemented
degraded browsing path when filesystem observation, snapshot publication,
engine connectivity, and asynchronous UI work overlap.

## ADDED Requirements

### Requirement: Filesystem observation cannot retire silently

When an observed directory can no longer be kept current, the engine SHALL
either recover the observation, perform a final refresh, or publish an explicit
unavailable outcome. A retired watch SHALL NOT leave its last rows presented as
unqualified current filesystem truth.

#### Scenario: Terminal watch failure

- **WHEN** a watched directory returns a terminal access, path, handle, or arm
  failure while shutdown is not requested
- **THEN** the engine SHALL perform the existing bounded final refresh or
  publish an explicit unavailable state and SHALL emit a structured diagnostic
  containing the operation, state, Win32 error, identity, and recovery action

#### Scenario: Delete and recreate a watched directory

- **WHEN** a browsed directory is deleted and later recreated at the same
  display path
- **THEN** the old observation SHALL not deliver late callbacks into the new
  observation, and a subsequent refresh SHALL publish the recreated directory
  as a new current state

#### Scenario: Shutdown races observation completion

- **WHEN** unwatch or engine shutdown wins while an overlapped completion or
  startup synchronization is pending
- **THEN** the observer SHALL terminate within its bounded shutdown contract,
  SHALL not fabricate a filesystem-change callback, and SHALL not access a
  closed handle or destructed callback target

### Requirement: Snapshot generations are committed atomically and ordered

The engine SHALL make a snapshot generation visible only after its complete
payload is initialized and valid. A failed publication SHALL preserve the last
known-good generation and SHALL NOT advance the visible generation. A client
SHALL reject older, wrong-context, or otherwise stale generations.

#### Scenario: Publication allocation or mapping failure

- **WHEN** creation, mapping, serialization, or commit of a replacement
  snapshot fails
- **THEN** the prior generation SHALL remain readable, the failed generation
  SHALL not be advertised as current, and the requesting surface SHALL receive
  an explicit refreshing or unavailable outcome rather than silently treating
  old rows as fresh

#### Scenario: Generations arrive out of order

- **WHEN** notification or remapping work for generation N completes after a
  newer generation N+1 has been accepted
- **THEN** generation N SHALL be discarded without replacing N+1 or changing
  the current freshness state

#### Scenario: Engine reconnects with a new snapshot context

- **WHEN** the engine disconnects and a replacement connection publishes a
  generation under a new context
- **THEN** dead mappings and pipe handles SHALL not be reused, the new context
  SHALL establish its own valid baseline, and a stale generation from the
  previous context SHALL not become visible

### Requirement: Asynchronous results are scoped to their request lifecycle

Search, refresh, and related asynchronous results SHALL carry enough identity
to prove that their query, scope, connection, and snapshot context is still
current before publication. Cancellation or supersession SHALL prevent a late
result from becoming the final visible state.

#### Scenario: Query supersession under sustained work

- **WHEN** query A is active and query B supersedes it before A completes
- **THEN** A SHALL be cancelled or discarded and only B may publish the final
  visible result

#### Scenario: Scope or snapshot changes during result consumption

- **WHEN** the active scope or accepted snapshot generation changes while a
  result list is being consumed
- **THEN** the old result SHALL not be applied to the new scope/generation and
  the surface SHALL transition through its explicit refreshing, cancelled, or
  unavailable state

#### Scenario: Close or disconnect while work is in flight

- **WHEN** a search surface closes, a client disconnects, or the engine stops
  while asynchronous work is active
- **THEN** cancellation SHALL be bounded, owned resources SHALL be released,
  no callback SHALL target a destroyed owner, and no partial result SHALL be
  published

### Requirement: Resilience diagnostics are structured and privacy-conscious

Resilience diagnostics SHALL identify the component, operation, state
transition, relevant non-sensitive identity, system error when available, and
recovery action. They SHALL use existing diagnostic categories/codes where
possible and SHALL NOT include arbitrary file contents or personal desktop
state.

#### Scenario: Failure can be classified from diagnostics

- **WHEN** a watch, publication, reconnect, stale-generation suppression,
  cancellation, or cleanup failure occurs
- **THEN** the diagnostic stream SHALL distinguish the failure and recovery
  outcome without requiring a screenshot or timing guess

#### Scenario: Diagnostic injection is test-only

- **WHEN** deterministic failure injection is compiled into a test target
- **THEN** it SHALL not be reachable from production or release-test runtime
  configuration and SHALL not change security, ACL, authentication, or wire
  compatibility behavior

### Requirement: Active-path verification is honest and bounded

The resilience verification capability SHALL use a uniquely marked disposable
fixture, deterministic seeds, bounded execution, and owned-process cleanup. It
SHALL return PASS, FAIL, or SKIPPED according to the result-envelope contract;
environmental inability SHALL never be converted into PASS.

#### Scenario: Fixture churn converges

- **WHEN** the capability performs deterministic create, rename, move, copy,
  delete, watch-cycle, refresh, and search operations
- **THEN** the final observed state SHALL be compared with an independent
  enumeration and any mismatch SHALL fail the capability with machine-readable
  evidence

#### Scenario: Optional desktop or engine restart is unavailable

- **WHEN** a GUI, provider, or owned-engine scenario cannot run in the current
  environment
- **THEN** the capability SHALL retain the headless coverage, emit an explicit
  SKIPPED/REQUIRED-BUT-UNAVAILABLE result for that scenario, and SHALL clean up
  all fixtures and owned processes

#### Scenario: Bounded resource soak

- **WHEN** the optional soak repeatedly mutates, refreshes, searches, and
  cycles observations for its configured duration
- **THEN** it SHALL sample process liveness and existing handle/thread/memory/
  CPU metrics, report monotonic growth or teardown failures, and SHALL not
  require byte-for-byte flat memory usage
