## Purpose

Provide a repeatable, evidence-producing black-box gate that proves the real
FastFiles executable remains usable through the highest-risk user workflows.

## ADDED Requirements

### Requirement: Real executable readiness

The E2E gate SHALL launch the intended FastFiles development executable and
shall prove both that the shell is discoverable through UI Automation and that
the expected unprivileged engine handshake becomes available within a bounded
timeout. A window handle alone SHALL NOT count as readiness.

#### Scenario: Launch reaches a usable state

- **WHEN** the gate starts FastFiles in an isolated disposable session
- **THEN** it records the executable identity, UIA main window, engine-ready
  observation, process IDs, and a bounded terminal result
- **AND** a loading or refreshing state that does not settle before the
  deadline fails the scenario with diagnostic evidence

### Requirement: User-visible navigation and search

The E2E gate SHALL drive real Home, fixture-directory, nested-directory,
back/forward/up, direct-path, restore, and search interactions through the
production UI path and SHALL verify the resulting visible UIA identities and
real fixture entries.

#### Scenario: Browse and restore a fixture

- **WHEN** a marked fixture is opened, a nested directory is activated, and the
  application is closed and relaunched in the same state directory
- **THEN** the gate observes the expected entries and navigation transitions,
  and the cold relaunch restores the previously active directory without an
  address-bar intervention

#### Scenario: Search settles and supersedes stale work

- **WHEN** the gate submits exact, partial, no-result, and rapidly replaced
  queries against deterministic fixture content
- **THEN** the visible Search.Results surface reaches a bounded result or
  explicit no-result terminal state, stale rows do not remain selected, and a
  real result can be activated

### Requirement: Storage and preview terminal behavior

The E2E gate SHALL exercise Storage Analysis and selection-driven preview on a
real fixture, SHALL reject indefinite Calculating/Loading states, and SHALL
record UIA geometry or a documented degraded terminal outcome when aggregate
data is unavailable.

#### Scenario: Storage analysis terminates

- **WHEN** Storage Analysis is opened and its treemap/list is selected for the
  fixture scope
- **THEN** the gate observes a terminal usable state, verifies exposed bounds
  stay within the storage content region when available, and fails with a
  screenshot and UIA dump if the surface spins past its deadline

#### Scenario: Preview follows asynchronous selection

- **WHEN** a deterministic previewable file is selected and then replaced by a
  different selection
- **THEN** the visible details/preview surface reaches Ready or an explicit
  terminal unsupported/disabled/error state, and stale preview content is not
  accepted as the new selection's result

### Requirement: Safe mutation and lifecycle evidence

The E2E gate SHALL perform file mutations only below its marker-protected
fixture root, verify the resulting filesystem state independently of UIA, and
shall exercise repeated close/relaunch/stop cycles with exact owned-process
cleanup.

#### Scenario: Disposable file operations

- **WHEN** the gate creates, renames, copies, moves, and deletes fixture items
  through FastFiles
- **THEN** each terminal operation is bounded and the expected paths are
  verified on disk; a path outside the owned fixture root is rejected before
  execution

#### Scenario: Repeated process lifecycle

- **WHEN** the gate performs multiple launch, navigate, close, relaunch, and
  stop iterations
- **THEN** each iteration either reaches a recorded terminal result or fails
  with process/UIA diagnostics, and no owned FastFiles or FastFilesEngine
  process or control pipe remains afterward

### Requirement: Actionable failure artifacts

Every failed E2E scenario SHALL preserve a generated artifact directory that
contains a screenshot when a window exists, a bounded UIA tree dump, the tail
of the relevant diagnostics log, process identity information, the current
stage and timeout, and the expected-versus-observed state.

#### Scenario: Timeout produces evidence

- **WHEN** a bounded E2E wait expires or an unexpected process/UIA outcome is
  observed
- **THEN** the result identifies the operation and elapsed time and points to
  the generated artifact directory without modifying tracked source files
