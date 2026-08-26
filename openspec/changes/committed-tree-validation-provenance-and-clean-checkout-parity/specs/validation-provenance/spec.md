# validation-provenance — spec delta

## ADDED Requirements

### Requirement: Qualifying local validation is bound to exact repository state

A local validation result used to qualify a Git candidate SHALL record the full
candidate commit SHA and commit-tree SHA and SHALL prove that relevant source state
was clean and unchanged throughout the qualifying run.

#### Scenario: Clean unchanged candidate qualifies

- **WHEN** exact-tree validation begins and ends on the same clean commit/tree with
  no non-ignored source delta
- **THEN** the result records exact-tree qualification for that full SHA/tree

#### Scenario: Dirty development result remains non-qualifying

- **WHEN** a validation PASS reads staged, unstaged, deleted, or non-ignored
  untracked source state, or runs explicitly in development mode
- **THEN** the PASS may remain development feedback but SHALL NOT be represented as
  exact-commit qualification evidence

### Requirement: Source mutation invalidates qualification

The system SHALL compare source identity at qualifying-run start and finish rather
than relying only on final worktree cleanliness.

#### Scenario: File changes during validation

- **WHEN** the HEAD, commit tree, or deterministic source-state digest changes while
  exact-tree validation is running
- **THEN** qualification fails with a stable provenance reason code

### Requirement: Selected evidence cannot be replayed against another candidate

Status and release-frontier consumers SHALL re-bind explicitly selected local
validation evidence to the candidate repository identity at consumption time.

#### Scenario: Result from commit A selected at commit B

- **WHEN** a PASS records a different full commit or tree SHA than the current
  candidate
- **THEN** the consumer fails closed with a provenance mismatch and does not infer
  local qualification

#### Scenario: Legacy PASS has no exact-tree provenance

- **WHEN** a selected PASS predates the provenance contract or is marked development
  mode
- **THEN** it is classified development-only/non-qualifying instead of silently
  certifying the current commit

### Requirement: Committed-tree parity is locally reproducible without destruction

The system SHALL provide an offline local mechanism that validates the exact commit
in a separately materialized source tree without resetting, cleaning, stashing, or
otherwise modifying the caller's worktree.

#### Scenario: Dirty caller worktree

- **WHEN** the caller has local work and requests committed-tree qualification
- **THEN** the qualifier materializes HEAD in a uniquely owned detached worktree,
  validates that committed tree, and leaves caller work untouched

#### Scenario: Cleanup ownership is uncertain

- **WHEN** the expected temporary destination is pre-existing or cannot be proven to
  be the worktree created by the current invocation
- **THEN** the qualifier refuses destructive cleanup and reports a failure

### Requirement: Generated ignored output is not source dirtiness

Ignored build, verifier-run, status, baseline, and signing outputs SHALL NOT make an
otherwise exact committed source tree non-qualifying merely by existing. The
provenance calculation SHALL NOT read ignored evidence directories to determine
source identity.

#### Scenario: Ignored build output exists

- **WHEN** only ignored generated files differ from the committed checkout
- **THEN** source cleanliness remains eligible for exact-tree qualification

### Requirement: Provenance diagnostics are deterministic and path-safe

Provenance evidence SHALL use stable reason codes, deterministic ordering, full
content identities, and repository-relative/sanitized diagnostics rather than raw
diffs or private absolute paths.

#### Scenario: Adversarial fixture matrix

- **WHEN** the hermetic Git fixture suite exercises clean, dirty, stale, mutated,
  detached, and missing-repository states
- **THEN** results are deterministic, false-green cases are rejected, ignored-output
  positives pass, and emitted provenance contains no absolute fixture path
