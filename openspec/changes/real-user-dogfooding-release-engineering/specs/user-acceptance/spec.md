## Purpose

Provides a repeatable, disposable way to exercise FastFiles as an end user, compare visible behavior with filesystem truth, and retain honest evidence across environments and sessions.

## ADDED Requirements

### Requirement: Disposable acceptance fixtures are deterministic and bounded
The acceptance tooling SHALL generate small, medium, and host-bounded large filesystem fixtures outside the repository, using stable names, contents, timestamps, attributes, and a manifest so a fixture can be reset without touching unrelated user data.

#### Scenario: Generate and reset a small fixture
- **WHEN** a tester requests the small profile in an empty disposable root
- **THEN** the tool creates the manifest-defined files and directories, reports the root and manifest, and can remove or recreate only that root

#### Scenario: Large profile respects host limits
- **WHEN** a tester requests a medium or large profile with an explicit item budget
- **THEN** the tool stops at the budget or reports an unavailable-resource condition before creating data outside the requested root

### Requirement: End-user acceptance scenarios are structured and evidence-bearing
The acceptance suite SHALL represent each scenario with an identifier, preconditions, ordered steps, expected behavior, automation status, environment requirements, and evidence references; a scenario SHALL distinguish PASS, FAIL, and an externally blocked or unavailable prerequisite.

#### Scenario: Record a successful workflow
- **WHEN** a launch, navigation, search, persistence, accessibility, or file-operation workflow completes
- **THEN** the record includes the fixture/profile, executable/build identity, observed result, independent evidence, and the scenario status

#### Scenario: Record an unavailable prerequisite
- **WHEN** a scenario requires signing, elevation, a clean host, a service, a network share, or an interactive desktop that is unavailable
- **THEN** the record is marked unavailable with the exact prerequisite and is not counted as a pass

### Requirement: File-operation acceptance compares against independent filesystem truth
Acceptance automation SHALL verify copy, move, rename, conflict, cancellation, delete/restore, undo, and failure-injection outcomes using an independent filesystem manifest or Win32/PowerShell observation rather than trusting only FastFiles UI state.

#### Scenario: Operation reports success with matching contents
- **WHEN** a disposable-file operation completes successfully
- **THEN** the independent manifest confirms the expected names, contents, attributes, and absence/presence of source and destination items before the scenario is marked PASS

#### Scenario: Operation result disagrees with the filesystem
- **WHEN** FastFiles reports success, failure, or cancellation but the independent observation disagrees
- **THEN** the scenario is marked FAIL with the mismatch captured as a dogfood finding and no success status is inferred

### Requirement: Acceptance automation remains honest under degraded mode and UI limitations
Repeatable automation SHALL use semantic/UIA or product test seams where available, SHALL keep degraded/unprivileged browsing usable, and SHALL report a bounded, named skip when a required external interaction cannot be performed; it MUST NOT add a hidden automation-only product path.

#### Scenario: Run core acceptance with the service unavailable
- **WHEN** the privileged service cannot authenticate or is not installed
- **THEN** Home, navigation, search over available degraded data, settings, and disposable file operations remain testable, and the evidence identifies degraded mode

#### Scenario: Semantic control is unavailable
- **WHEN** a host cannot expose a required UIA control or external drag source
- **THEN** the automation records the missing control/prerequisite and stops that scenario without substituting pixel coordinates or claiming PASS

### Requirement: Performance and resource evidence uses repeatable workloads
The campaign SHALL capture measured launch, folder-open, search, preview/storage, and bounded soak observations with workload scale, build identity, environment, timing method, and resource samples; it SHALL distinguish a baseline, a regression, and an unexecuted workload.

#### Scenario: Compare a large-directory baseline
- **WHEN** the same fixture profile is opened on two candidate builds or configurations
- **THEN** the evidence reports comparable latency and resource measurements with the fixture and environment identified

#### Scenario: Detect monotonic resource growth
- **WHEN** a bounded soak loop repeats navigation, search, preview, and disposable operations
- **THEN** the evidence records periodic working set, private bytes, handles, threads, CPU, and process identity, and flags monotonic growth for investigation
