## Purpose

Make repository status claims, current-head certification, and performance statements depend on evidence from the implementation under test rather than on inherited labels.

## ADDED Requirements

### Requirement: Significant claims are classified against current evidence

The campaign SHALL keep a durable truth ledger in this change's evidence. For every significant claim it relies on or corrects, the ledger SHALL record the claim, the evidence used, and exactly one of `VERIFIED_CURRENT`, `VERIFIED_WITH_LIMITATION`, `STALE`, `FALSE`, `UNPROVEN`, `BLOCKED_EXTERNAL`, or `NOT_APPLICABLE`. A historical pass, a checked task, a workstate sentence, or a merged pull request SHALL NOT by itself raise a claim to `VERIFIED_CURRENT`.

#### Scenario: A stale handoff cannot certify current main

- **WHEN** a workstate or handoff says a pull request is unmerged, a candidate is current, or no local action remains
- **AND** live Git, the pull request, or the built binaries disagree
- **THEN** the ledger SHALL record the contradiction
- **AND** SHALL NOT treat the handoff sentence as current certification

#### Scenario: An unmeasured claim stays unproven

- **WHEN** a certification count, benchmark, or runtime result is cited from another SHA or from a document only
- **THEN** the ledger SHALL classify that claim as `UNPROVEN` for the current implementation
- **AND** SHALL NOT copy the old number forward as a current result

### Requirement: Local certification names the implementation candidate

After any production-source change, and before this campaign reports current `main` as certified, local certification SHALL run against the exact implementation SHA that produced the binaries. The candidate tree SHALL be clean. Every certification-relevant executable that embeds build identity SHALL embed that SHA. A documentation-only tip SHALL be reported separately. Historical results, including results for `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`, SHALL NOT be reported as certification of a different SHA.

#### Scenario: A documentation tip is not the executable candidate

- **WHEN** commits after the last rebuilt implementation SHA change only documentation or campaign evidence
- **THEN** the report SHALL name the embedded implementation SHA
- **AND** SHALL NOT say the documentation tip is exact-head certified unless those binaries were rebuilt from that tip and the binding gate passed

#### Scenario: A stale or empty identity fails closed

- **WHEN** a certification artifact embeds a different SHA, an empty identity, or an overridden identity that is not the candidate
- **THEN** qualification SHALL reject that artifact
- **AND** SHALL NOT report a pass for it

### Requirement: Performance claims require a same-method comparison

The campaign MUST NOT claim that a build is faster, leaner, or more responsive without a before and after measurement of the same metric, on the same machine, build type, configuration, dataset, and method. The record SHALL include the baseline SHA, the candidate SHA, the fixture, the repetition count, and the before and after results. A debug-only timing SHALL NOT be reported as product performance unless the metric exists only in that configuration. An optimization that does not improve the measured metric SHALL be reverted unless a separate requirement in this change justifies keeping it.

#### Scenario: A microbenchmark speedup is not a product claim

- **WHEN** a change improves an isolated microbenchmark but the user-visible or operator-visible metric was not measured before and after
- **THEN** the campaign SHALL NOT report a product performance improvement
- **AND** SHALL keep the unrelated microbenchmark result labeled as such

#### Scenario: A non-improving optimization is not retained for its own sake

- **WHEN** the same-method measurement shows no improvement in the targeted metric
- **AND** the change has no other proven correctness, reliability, or safety benefit
- **THEN** the campaign SHALL revert that optimization
- **AND** SHALL record the negative result

### Requirement: Product edits require proof and preserve surrounding behavior

The campaign SHALL NOT change production source for a speculative defect, an unmeasured hotspot, or a documentation impression. A production edit SHALL have a failing reproducer, a measured hotspot, or a directly observed runtime defect recorded first. The edit SHALL NOT weaken durability, index correctness, accessibility, release provenance, or the closed privileged-command surface. A speedup that causes incorrect results, a crash, a major memory regression, or a significant tail-latency regression SHALL NOT be accepted.

#### Scenario: An unproven hotspot is not edited

- **WHEN** a path is suspected to be slow but has no current measurement or failing behavior
- **THEN** the campaign SHALL measure or record it as unproven
- **AND** SHALL NOT change production source for that suspicion alone

#### Scenario: A security or durability regression blocks acceptance

- **WHEN** a candidate improves a measured metric but fails a correctness, durability, accessibility, provenance, or closed-command-surface check
- **THEN** the campaign SHALL reject that candidate
- **AND** SHALL NOT weaken the failing control to keep the speedup

### Requirement: External boundaries stay external

The campaign SHALL NOT claim a finite completion bound for arbitrary synchronous raw-volume device-control calls. Requesting cancellation, a responsive control path, worker exit, and kernel I/O completion SHALL remain distinct. Inherited tasks that leave that bound unproven SHALL remain unchecked. The campaign SHALL NOT change billing, spending limits, account plan, branch protection, production signing, or trust-store configuration. A hosted run whose jobs fail before any step executes SHALL be classified as `BLOCKED_EXTERNAL`, not as a product-test pass or product-test failure. Local parity SHALL be labeled local.

#### Scenario: A cooperative fixture does not prove driver completion

- **WHEN** a test simulates or cancels a non-volume operation, or a volume call returns in a fixture
- **THEN** the raw-volume completion bound SHALL remain `UNPROVEN`
- **AND** the inherited unchecked tasks for that bound SHALL NOT be marked complete

#### Scenario: A zero-step hosted run is not relaunched as if it were a product failure

- **WHEN** the required hosted jobs for a pushed SHA fail before any workflow step with an account-payment or spending-limit annotation
- **THEN** the ledger SHALL record that run id, SHA, and annotation as `BLOCKED_EXTERNAL`
- **AND** the campaign SHALL NOT change billing or account settings to make the jobs start
- **AND** SHALL NOT dispatch another run of that same known-unstartable workflow solely to obtain a different label

### Requirement: The overnight branch is reviewable and not self-merged

Campaign commits SHALL land on the dedicated overnight branch and SHALL be pushed without force. The campaign SHALL open or update a reviewable pull request. It SHALL NOT merge that branch into `main` automatically. It SHALL NOT force-push, rewrite published history, or delete unrelated user files. The pre-existing detached worktree at `0dc3356` SHALL remain untouched.

#### Scenario: Local completion does not merge the branch

- **WHEN** local certification and the performance record are complete
- **AND** hosted jobs still cannot execute
- **THEN** the pull request SHALL remain open for review
- **AND** the campaign SHALL NOT merge it into `main`

#### Scenario: Unrelated work is preserved

- **WHEN** the campaign prepares a clean tree for certification
- **THEN** it SHALL leave unrelated dirty files, untracked user files, and the detached `0dc3356` worktree in place
- **AND** SHALL NOT reset or delete them to manufacture a clean status
