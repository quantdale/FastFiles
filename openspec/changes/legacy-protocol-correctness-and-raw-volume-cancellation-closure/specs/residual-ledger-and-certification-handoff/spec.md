## Purpose

Reconcile the residual task ledger with evidence, refresh the external certification inventory, and produce an A–J handoff that never states a stronger guarantee than the executable evidence establishes.

## ADDED Requirements

### Requirement: Inherited residual tasks stay open until their guarantees are proven

Predecessor change `final-residual-hardening-and-hosted-ci-recovery` tasks 2.3, 5.5, 6.1, 6.2, and 6.6 MUST be restated as open work in this campaign. They MUST NOT be checked off by supersession, documentation-only commits, or cooperative simulated-driver tests. The raw-volume completion-bound task MUST remain unresolved unless a genuine finite completion bound is established. Historical work completion MUST be distinguished from current validation.

#### Scenario: Raw-volume bound remains open without a proven deadline

- **WHEN** cancellation tests pass for named-pipe or cooperative simulated I/O but an arbitrary synchronous raw-volume `DeviceIoControl` still has no proven finite completion bound
- **THEN** the inherited completion-bound task remains unchecked
- **AND** the handoff classifies it as `UNPROVEN` rather than locally complete

#### Scenario: Hosted push and inspect remain open until live evidence exists

- **WHEN** hosted GitHub Actions has not produced a complete six-job result for the candidate SHA
- **THEN** inherited hosted-inspect tasks remain unchecked
- **AND** are classified `BLOCKED_EXTERNAL` when the block is billing or availability

### Requirement: External certification inventory stays independently bounded

The existing external-frontier inventory MUST be rechecked against the current implementation. The following boundaries MUST remain independent: production signing and authorized pins; clean-host install/uninstall; elevated live MFT/USN; physical DPI, High Contrast, and mixed-monitor testing; authorized disposable guest execution; hosted GitHub Actions. Production trust material MUST NOT be provisioned, and privileged installation MUST NOT be performed, without the necessary authority. Each unresolved requirement MUST name its canonical command, prerequisites, expected evidence, and the exact action that unlocks it.

#### Scenario: Each remaining external boundary has an unlock action

- **WHEN** the handoff lists an unresolved external certification requirement
- **THEN** the inventory records the prerequisite, canonical command, expected evidence, success criterion, and unlock action
- **AND** does not report that requirement as passed

### Requirement: Closure report is evidence-backed and classified

The campaign MUST produce one report covering repository and integration state; legacy protocol; raw-volume cancellation; hosted CI; branch protection; validation; adversarial review; external certification; remaining work classified as `LOCAL_ACTIONABLE`, `BLOCKED_EXTERNAL`, `UNPROVEN`, `SPECULATIVE`, or `FUTURE_PRODUCT_WORK`; and a final verdict that states separately local implementation status, local certification status, hosted CI status, integration/merge readiness, privileged runtime certification, and production release certification. The report MUST NOT state any stronger guarantee than the executable evidence establishes. Documentation-only commits MUST NOT be created merely to trigger additional workflows.

#### Scenario: Verdicts are stated separately

- **WHEN** local tests are green and hosted CI is billing-blocked
- **THEN** the report states local certification independently of hosted CI
- **AND** does not report hosted or production-release certification as passed
