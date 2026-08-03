## ADDED Requirements

### Requirement: Single Resumable Autonomous Entry Point
The system SHALL provide one documented agent-operable entry point (`verify/intake.ps1` with an `autonomous` verb) that drives the full lifecycle `discover -> plan -> provision -> implement -> build -> test -> diagnose -> repair -> re-test -> validate -> collect-evidence -> update-tasks -> commit -> sync -> archive` noninteractively, with persistent run state that survives crashes and resumes across future sessions from the last incomplete phase.

#### Scenario: A run resumes after an interruption
- **WHEN** an autonomous run is interrupted (process exit, crash) and later re-invoked
- **THEN** the orchestrator SHALL load its persisted state and resume at the last incomplete phase without re-running already-completed phases or losing collected evidence

#### Scenario: The loop is bounded and non-interactive
- **WHEN** the autonomous verb runs
- **THEN** it SHALL complete without prompting for interactive input, SHALL halt at or before a hard iteration cap, and SHALL return a deterministic exit code

### Requirement: Persistent Run State And Machine-Readable Status
The orchestrator SHALL persist run state to `verify/runs/autonomous/<run-id>/state.json` recording the current phase, completed step identifiers, per-task evidence references, failure classification, and bounded retry counts; and SHALL expose machine-readable status (current phase, open-task count, last outcome) so an agent or caller can branch without parsing prose.

#### Scenario: Status is queryable without parsing logs
- **WHEN** an agent queries the run status
- **THEN** it SHALL receive a machine-readable document identifying the current phase, completed and remaining steps, and the authoritative open-task count

### Requirement: Failure Classification And Bounded Retries
The orchestrator SHALL classify each failure as Class A (harness/config/environment), Class B (product source), or external-impossibility, SHALL apply only Class A fixes automatically, SHALL surface Class B fixes with a recorded root cause and diff for review, and SHALL stop and escalate on a recurring normalized failure signature without retrying the same action unchanged.

#### Scenario: A recurring failure stops the loop
- **WHEN** the same normalized failure signature recurs on a subsequent iteration
- **THEN** the orchestrator SHALL stop and escalate with captured diagnostics rather than continue retrying

#### Scenario: An external impossibility is recorded, not fabricated
- **WHEN** a requirement depends on a physical, legal, commercial, or secret external capability that cannot be obtained or replaced after exhausting secure lawful workarounds
- **THEN** the orchestrator SHALL preserve machine evidence of the limitation, complete every unaffected task, implement the full pluggable path, keep the affected tasks as `REQUIRED-BUT-UNAVAILABLE`, and SHALL NOT mark them passed

### Requirement: Future-Agent Intake Contract
The repository SHALL contain documentation (`AUTONOMOUS.md` and an `AGENTS.md` section) and a machine-readable contract (`verify/autonomous/contract.json`) describing the one entry point, the state file, the dependency graph, exit codes, and a future-agent intake sequence (inspect state, convert a new request into an OpenSpec change, implement, provision, generate/update tests, run focused and full verification, diagnose/repair, update evidence, commit, push when an authenticated remote is available, sync and archive, leave resumable state if interrupted).

#### Scenario: A future agent bootstraps without human orientation
- **WHEN** a new agent session inspects the repository
- **THEN** it SHALL discover the autonomous entry point, the resume protocol, and the intake sequence from the documentation and contract without requiring human explanation