## Purpose

Hosted GitHub Actions on this private repository is an external merge hold. Local verification and a documentation tip are reported separately from a hosted result on the executable candidate.

## ADDED Requirements

### Requirement: Blocked hosted jobs are not a pass and not permission to merge

The hosted workflow's six executions SHALL be `debug`, `release-test`, `analyze`, `asan-protocol-ipc-lifecycle`, `repository-hygiene-and-contracts`, and `unsigned-release-candidate-qualification`. When those jobs fail before any step because account payments have failed or a spending limit needs to be increased, the campaign SHALL record hosted CI as externally blocked, SHALL NOT convert that result to pass, and SHALL NOT merge the candidate to `main`. The campaign SHALL NOT change billing, spending limits, payment methods, plans, branch protection, or rulesets. The campaign SHALL NOT dispatch further runs of a workflow already shown not to start. When the protection or ruleset API returns an access error, the campaign SHALL record that error and SHALL NOT treat the missing rule as permission to bypass the hosted jobs.

#### Scenario: The current billing failure blocks merge

- **WHEN** a hosted run for the candidate reports all six executions failed, each with no steps, and the billing or spending-limit annotation
- **THEN** the recorded hosted result SHALL be externally blocked
- **AND** the candidate SHALL NOT be merged to `main`

#### Scenario: An unreadable protection API is not a bypass

- **WHEN** a read of branch protection or rulesets fails because the repository plan does not allow that API
- **THEN** the campaign SHALL record the failure
- **AND** SHALL NOT merge while the hosted jobs are blocked

#### Scenario: Known-unstartable workflows are not retried in a loop

- **WHEN** the billing block is still in effect at campaign start
- **THEN** the campaign SHALL recheck availability at start and once after a candidate push
- **AND** SHALL NOT keep dispatching runs that cannot start

### Requirement: Local parity and a documentation tip are not hosted certification

A local CI parity result SHALL be labeled local and SHALL NOT be reported as a hosted pass. If the six hosted jobs start, each applicable job SHALL run and pass on the exact executable candidate before merge. A later documentation-only revision SHALL NOT be described as that certified executable. Executable-tree equivalence SHALL be recorded before anyone treats a documentation tip as the same candidate.

#### Scenario: Local parity stays local

- **WHEN** local CI parity passes and the hosted jobs are externally blocked
- **THEN** the local result SHALL remain a local result
- **AND** SHALL NOT be cited as hosted certification

#### Scenario: Hosted success is tied to the executable candidate

- **WHEN** hosted jobs are able to start
- **THEN** merge SHALL wait until all six executions have run and passed for the executable candidate's revision
- **AND** a documentation commit that is not that executable revision SHALL NOT satisfy the requirement unless executable-tree equivalence with the qualified binaries is recorded
