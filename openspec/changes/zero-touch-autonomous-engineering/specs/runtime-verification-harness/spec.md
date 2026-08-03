## ADDED Requirements

### Requirement: Disposable Snapshot-Restorable VM Environment Provider
The harness SHALL provide a disposable, snapshot-restorable environment provider implementing `provision -> activate -> collect-logs -> cleanup -> snapshot-restore`, that provisions a clean Windows guest (searching approved local media first; if none and network/licensing permit, obtaining official Windows evaluation media automatically with checksum/signature verification; unattended install; temporary automation account; WinRM or another secure management channel; build/test prerequisites; a clean reference snapshot), and SHALL validate that the same capability runs unchanged across the `local` and the disposable provider with logs and artifacts collected back to the run tree.

#### Scenario: A capability runs unchanged across local and a disposable provider
- **WHEN** a capability is run against the `local` provider and then against the disposable VM provider
- **THEN** it SHALL execute without source changes in both contexts and the run tree SHALL collect logs and artifacts from both

#### Scenario: A disposable provider is restored to a clean snapshot after a run
- **WHEN** a run against the disposable provider completes (success or failure)
- **THEN** the provider SHALL restore the guest to its clean reference snapshot and SHALL leave no leftover installation, service, task, or account on the guest

### Requirement: Lawful Fallback When Media Is Unavailable
If approved local media is absent and official Windows evaluation media cannot be lawfully obtained (network or licensing restrictions), the harness SHALL evaluate Windows Sandbox, an existing interactive runner, a newly created disposable VM, or a supported cloud runner, and SHALL select the first secure, lawful, reversible option that satisfies the tests; if none is available, it SHALL record machine evidence of the limitation, complete every unaffected task, and keep VM-gated tasks as `REQUIRED-BUT-UNAVAILABLE` rather than fabricated.

#### Scenario: A lawful fallback is chosen when the primary route is unavailable
- **WHEN** the primary VM provisioning route is unavailable
- **THEN** the harness SHALL evaluate the documented fallbacks in order and select the first secure lawful option, recording which was chosen

#### Scenario: A genuine VM impossibility is recorded, not fabricated
- **WHEN** no secure lawful disposable environment can be provisioned
- **THEN** the harness SHALL record machine evidence of the limitation and keep VM-gated tasks `REQUIRED-BUT-UNAVAILABLE` rather than pass them

### Requirement: Resumable Run State And Crash Recovery
The harness SHALL persist autonomous run state across phases so that an interrupted run (process exit or crash) resumes at the last incomplete phase on re-invocation, with completed phases not re-run and collected evidence preserved.

#### Scenario: A crashed autonomous run resumes
- **WHEN** an autonomous run crashes and is re-invoked
- **THEN** it SHALL resume at the last incomplete phase and SHALL preserve previously collected evidence

### Requirement: Flaky-Test Root-Cause Policy
The harness SHALL, on any intermittent test failure, preserve the initial failure output, reproduce the failure under stress, capture timing/resource/thread/process/environment data, identify the root cause (race, isolation leak, stale object, ordering, or environmental dependency), fix the root cause where reasonable, add regression coverage, and run repeated clean tests to establish stability; a "passed on retry" SHALL NOT be a terminal pass without a root-cause fix or a documented non-determinism bound.

#### Scenario: An intermittent failure is root-caused, not masked by retry
- **WHEN** a test fails intermittently and passes on retry
- **THEN** the harness SHALL capture the failure data, identify and fix the root cause where reasonable, add regression coverage, and run repeated clean tests before accepting a pass