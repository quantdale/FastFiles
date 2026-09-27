## Purpose

Guarantee that a scan which did not complete is never represented as successful merely because a negotiated peer cannot understand `ScanFailed`, while keeping successful legacy wire bytes and modern `{2,2}+` failure-terminal semantics intact.

## ADDED Requirements

### Requirement: Investigation reproduces mixed-version failure before any correction

A deterministic test MUST exercise a negotiated protocol version that does not support `ScanFailed` together with an interrupted or failed scan. The test MUST establish whether the service emits `ScanComplete` despite incomplete enumeration, and whether an engine at that negotiated version would mark the scan authoritative, advance durable state incorrectly, or finalize an incomplete reconciliation pass. Normal success and failure MUST be exercised separately. If a real defect exists, the reproducer MUST fail against the original behavior. The defect MUST NOT be treated as proven from source inspection alone.

#### Scenario: Legacy failed scan is reproduced before correction

- **WHEN** a test negotiates a pre-{2,2} protocol and a scan fails during setup or mid-enumeration
- **THEN** the test records the terminal message type actually emitted and the engine's durable completion/reconciliation outcome
- **AND** if that outcome treats the incomplete scan as successful, the same test MUST fail against the uncorrected implementation

#### Scenario: Legacy successful scan remains a separate case

- **WHEN** a test negotiates a pre-{2,2} protocol and enumeration reaches a genuine end
- **THEN** the test MUST show `ScanComplete` and authoritative completion, independently of the failed-scan case

### Requirement: Incomplete scans are never successful terminals for any negotiated version

A scan that did not complete MUST never be represented as successfully completed merely because the peer cannot understand `ScanFailed`. The service MUST NOT send `ScanFailed` to a peer that cannot parse it. The service MUST NOT send `ScanComplete` as a stand-in for failure. A safe correction MUST terminate the affected connection and require recovery rather than emit a misleading successful terminal, unless investigation of the complete lifecycle proves a narrower equivalent that still satisfies this invariant.

#### Scenario: Legacy failed scan cannot be a successful completion

- **WHEN** a peer negotiated a protocol version that does not support `ScanFailed` and the scan cannot run to completion
- **THEN** the service MUST NOT send `ScanComplete` for that scan
- **AND** the service MUST NOT send `ScanFailed`
- **AND** the engine MUST NOT mark the volume scan complete from that attempt

#### Scenario: Modern peers still receive ScanFailed

- **WHEN** a peer negotiated a protocol version that supports `ScanFailed` and the scan cannot run to completion
- **THEN** the service MUST send `ScanFailed` and MUST NOT send `ScanComplete`

#### Scenario: Successful scans keep their current terminals

- **WHEN** enumeration ends because the scan ran to genuine completion
- **THEN** the service MUST send `ScanComplete` for every supported negotiated version, including pre-{2,2} peers
- **AND** the wire bytes a legacy peer receives for that successful scan MUST remain unchanged

### Requirement: Incomplete coverage never deletes valid durable records

Interrupted or failed reconciliation MUST NOT delete valid records based on incomplete coverage. Recovery MUST NOT reuse stale connection-scoped volume identities. Version negotiation MUST remain compatible with the documented supported matrix (same major, or exactly one major behind). Successful-scan behavior, authentication, durable cursor correctness, bounded retry, and current `{2,2}` `ScanFailed` semantics MUST be preserved.

#### Scenario: Failed legacy scan aborts reconciliation instead of finishing it

- **WHEN** a failed scan involving a pre-{2,2} negotiation occurs while a reconciliation pass is active
- **THEN** the engine MUST abort the pass without computing or removing durable entries
- **AND** MUST NOT advance the reconciliation clock from that failure

#### Scenario: Recovery uses a fresh connection

- **WHEN** a legacy failed scan ends by tearing down the connection
- **THEN** the next privileged connection MUST obtain new connection-scoped volume identities
- **AND** MUST NOT apply the previous connection's handles or in-flight scan authority
