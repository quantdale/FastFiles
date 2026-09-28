## Purpose

Local certification names the executable candidate it actually built, and hosted, merge, and raw-volume claims stay separate from that local result.

## ADDED Requirements

### Requirement: Local certification is bound to the implementation candidate

After the production-source fixes in this change are committed, local certification SHALL be run against the exact implementation SHA that produced the binaries under test. The working tree for that candidate SHALL be clean. Every certification-relevant executable that embeds build identity SHALL embed that SHA. A historical candidate, including any SHA certified before those production-source commits, SHALL NOT be reported as certification of the newer source. Documentation commits SHALL NOT be added between the implementation candidate and the completion of executable certification. A stale, empty, or overridden identity SHALL be rejected.

#### Scenario: A newer source tree cannot reuse an older candidate

- **WHEN** production source has changed since a previously certified SHA
- **THEN** certification SHALL name the new implementation SHA
- **AND** SHALL NOT treat the older SHA's logs as evidence for the newer binaries

#### Scenario: Identity overrides fail closed

- **WHEN** a certification run is pointed at a stale identity, an empty identity override, or a binary whose embedded identity is not the candidate SHA
- **THEN** the qualification SHALL reject that artifact
- **AND** SHALL NOT report a pass for it

### Requirement: Hosted CI and merge stay distinct from local parity

Local CI parity SHALL be labeled local. It SHALL NOT be called hosted CI. A hosted run whose jobs fail before any workflow step executes SHALL be classified as blocked external infrastructure, not as a product-test pass and not as a product-test failure. While the required hosted jobs cannot execute, the pull request SHALL remain unmerged. This change SHALL NOT alter billing, spending limits, account plan, branch protection, production signing, or trust-store configuration.

#### Scenario: Zero-step hosted jobs do not certify

- **WHEN** the required hosted jobs fail before any step executes
- **THEN** the result SHALL be recorded as hosted CI blocked by an external cause
- **AND** local passes SHALL remain labeled local
- **AND** the pull request SHALL NOT become merge-eligible on the strength of those local passes

### Requirement: The raw-volume completion bound stays unproven

This change SHALL NOT claim that a synchronous raw-volume device-control call has a proven finite completion bound. Requesting cancellation, observing a responsive control plane, observing worker-thread exit, and observing kernel I/O completion SHALL remain distinct. Inherited tasks that record that bound as unproven SHALL NOT be marked complete because this change exists.

#### Scenario: Local scan certification does not close the raw-volume bound

- **WHEN** local certification of the retry and focus fixes passes
- **THEN** the raw-volume completion bound SHALL still be reported as unproven
- **AND** the predecessor tasks that track that bound SHALL remain open
