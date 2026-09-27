## Purpose

Keep hosted GitHub Actions certification, branch-protection facts, and privileged-service integration policy honest while Actions may be billing-blocked, without treating local CI parity as hosted certification.

## ADDED Requirements

### Requirement: Local parity is not hosted certification

Hosted certification MUST name the workflow, run, trigger, exact SHA, required jobs, results, skips, and artifacts. A billing or availability rejection before jobs run MUST be recorded as `HOSTED_CI = BLOCKED_EXTERNAL`. A local parity report MUST NOT be called hosted certification. The agent MUST avoid repeated known-unstartable hosted runs. Account billing, payment settings, and repository permissions MUST NOT be changed by this campaign.

#### Scenario: Hosted jobs execute for the candidate SHA

- **WHEN** GitHub Actions accepts a run for the final pushed candidate SHA
- **THEN** certification inspects every required job in the current workflow topology and retained artifacts
- **AND** reports hosted success only if that complete required matrix succeeds for that SHA

#### Scenario: Hosted jobs are rejected before execution

- **WHEN** the current run annotation shows an account-level billing or availability block
- **THEN** the report records the actual run and annotation as `BLOCKED_EXTERNAL`
- **AND** provides the repository owner's unlock action
- **AND** does not dispatch additional known-unstartable workflows

### Requirement: Integration waits for required hosted checks

Privileged-service changes MUST land on an isolated feature branch or worktree. Substantial privileged-service changes MUST NOT merge directly into unprotected `main` while required hosted checks cannot execute. Local gates and independent review MUST complete, then a reviewable pull request MUST be prepared. If hosted CI remains blocked, the pull request MUST remain ready for certification rather than treating local parity as authorization to bypass required hosted checks.

#### Scenario: Feature branch is reviewable while hosted CI is blocked

- **WHEN** local implementation and review are complete and GitHub Actions still rejects jobs before execution
- **THEN** the candidate remains on its feature branch with a reviewable pull request
- **AND** merge readiness is reported as waiting on hosted certification

### Requirement: Branch protection facts are observed, not fabricated

The campaign MUST inspect whether conventional branch protection, repository rulesets, or another integration policy actually applies to `main`. It MUST propose an appropriate policy requiring code review and passing checks for privileged-service changes. It MUST NOT weaken checks or change account subscription settings to make an unavailable feature appear enabled, and MUST NOT silently alter repository permissions.

#### Scenario: Protection state is recorded from the live repository

- **WHEN** the hosted-CI investigation inspects `main`
- **THEN** the report records the protections and rulesets that are actually present
- **AND** records any owner action still required
- **AND** does not claim a protective mechanism that the account cannot enable
