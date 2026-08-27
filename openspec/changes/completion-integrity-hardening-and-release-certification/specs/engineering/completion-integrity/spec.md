# Spec: Completion Integrity and Release Certification

## Purpose

Define the evidence and execution requirements for declaring FastFiles locally complete and for separating that state from environment-bound production certification.

## ADDED Requirements

### Requirement: Completion claims are bound to the current committed tree

The system's engineering status MUST NOT claim local completion from historical, dirty-worktree, pending, skipped, or differently-scoped validation evidence.

#### Scenario: Final implementation is committed
- **WHEN** a campaign reaches its final implementation candidate
- **THEN** exact-tree qualification and the required build/test/policy gates MUST run against that committed SHA or an explicitly provenance-equivalent tree

#### Scenario: Hosted CI is still pending
- **WHEN** the exact pushed SHA has a hosted run that is queued or in progress
- **THEN** the corresponding completion task MUST remain incomplete and MUST NOT be described as PASS

### Requirement: Active engineering requirements cannot disappear as a substitute for completion

Still-required active tasks MUST remain represented until satisfied, explicitly superseded, or removed by a reviewed design/spec revision that explains why the requirement is no longer applicable.

#### Scenario: A task has missing evidence
- **WHEN** an active task is required by the current proposal/design/spec but its proof is incomplete
- **THEN** the task remains open even if project-status would otherwise report zero active-local tasks

#### Scenario: A requirement is intentionally retired
- **WHEN** engineering evidence shows an active requirement is obsolete or no longer applicable
- **THEN** the design/spec MUST record the reason and replacement/supersession before the task is removed

### Requirement: Material defect closure includes a decisive regression test

Every material defect fixed during completion convergence MUST have a deterministic regression that directly asserts the failed invariant unless the defect is inherently external and cannot be exercised locally.

#### Scenario: A test fixture cannot reach the failure state
- **WHEN** setup removes or bypasses the state that the test claims to validate
- **THEN** the test MUST NOT count as closure evidence

#### Scenario: Decisive assertion is disabled or relaxed
- **WHEN** the key assertion for a material invariant is commented out, skipped, or deliberately weakened to let current behavior pass
- **THEN** the corresponding requirement remains incomplete

### Requirement: Release qualification evidence is retained by hosted CI

A successful hosted unsigned release-candidate qualification MUST retain the safe candidate manifest and provenance artifacts needed to audit that exact run.

#### Scenario: Qualification succeeds but upload path is empty
- **WHEN** the release test passes but the workflow finds no manifest/provenance files at its upload path
- **THEN** the release-evidence gate is incomplete even though functional packaging checks passed

#### Scenario: Evidence is retained
- **WHEN** candidate qualification succeeds
- **THEN** hosted CI uploads non-secret manifest/provenance evidence whose commit identity matches the qualified tree

### Requirement: Sanitizer coverage has one authoritative definition

The repository MUST define one current sanitizer/fuzz/lifecycle slice and MUST keep CI, task ledgers, and completion evidence consistent with it.

#### Scenario: CI runs fewer tests than completion evidence claims
- **WHEN** hosted CI runs a two-test sanitizer slice while current completion evidence claims a required three-test slice
- **THEN** the discrepancy MUST be reconciled before completion

#### Scenario: A lifecycle target is intentionally excluded
- **WHEN** an existing lifecycle test cannot validly run under the sanitizer configuration
- **THEN** the technical reason and equivalent memory/lifetime assurance MUST be documented rather than silently omitting it

### Requirement: Fresh hardening audit follows known blocker closure

After known P1 blockers are fixed, the repository MUST undergo a fresh current-tree review of correctness/data integrity, security/privilege/IPC, concurrency/lifetime, resource/performance, UI/accessibility, build/release, and verification truth before local completion is declared.

#### Scenario: Historical finding exists
- **WHEN** an old audit reports a defect but current source has changed
- **THEN** the finding MUST be reproduced on the live tree before opening implementation work

#### Scenario: New material defect is found
- **WHEN** the fresh audit confirms a P0/P1 or significant P2 defect
- **THEN** it MUST be repaired or explicitly dispositioned with evidence before local completion

### Requirement: Local completion and production certification are distinct

FastFiles MUST NOT label itself fully production-certified when required signer, pin, elevated raw-volume, clean-host, provider, or physical-display evidence has not actually been obtained.

#### Scenario: All local gates pass but external evidence is unavailable
- **WHEN** no locally reproducible blocker remains and all repository-enforceable gates pass, but one or more required external prerequisites remain unavailable
- **THEN** the final state MUST be reported as **locally complete; external certification pending**

#### Scenario: External evidence is actually available
- **WHEN** the authorized environment supplies the required signer/pins/clean-host/elevated/provider/display evidence
- **THEN** those gates may be executed and recorded without weakening their security or provenance requirements

### Requirement: Autonomous execution stops on evidence, not elapsed time

The long-running campaign MAY continue for up to approximately twelve hours, but it MUST stop early when a fresh audit shows no material evidence-backed local work and MUST leave a safe checkpoint if the time budget ends before completion.

#### Scenario: Work finishes early
- **WHEN** all local blockers and hardening gates are closed before the nominal budget
- **THEN** run a fresh audit/certification pass and stop rather than manufacturing low-value changes

#### Scenario: Time budget ends with an unresolved blocker
- **WHEN** the session reaches its budget before a material blocker is solved
- **THEN** preserve a safe committed/pushed checkpoint and record one executable next frontier instead of falsely marking completion
