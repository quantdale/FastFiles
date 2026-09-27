## ADDED Requirements

### Requirement: ScanComplete is durable authority only for completed scans

The engine MUST treat `ScanComplete` as durable authority only when the scan actually ran to completion with every applied batch, was not paused, and is the in-flight scan for that connection-scoped volume. A failed, cancelled, disconnected, or incomplete scan MUST NOT cause `MarkScanComplete` and MUST NOT `FinishReconciliationPass`. `ScanFailed` MUST abort any active reconciliation pass without removals and MUST NOT mark the scan complete.

#### Scenario: Legacy false success cannot complete the index

- **WHEN** a pre-{2,2} negotiation would previously have delivered `ScanComplete` for a scan that did not finish
- **THEN** the engine MUST NOT mark the volume scan complete
- **AND** MUST NOT finalize a reconciliation pass from that attempt

#### Scenario: Disconnect during an in-flight scan is non-authoritative

- **WHEN** the privileged connection leaves Active while a scan is in progress and no successful `ScanComplete` was delivered
- **THEN** the engine MUST clear ephemeral connection-scoped state
- **AND** MUST NOT mark the volume scan complete from that interrupted attempt

### Requirement: Recovery after mixed-version failure uses fresh connection state

After a failed mixed-version scan ends the connection, the next Active period MUST re-derive durable identity from a fresh `EnumerateVolumes` and MUST NOT reuse the previous connection's volume handles. Bounded retry of delivered `ScanFailed` outcomes remains a rate/state bound, not a false-success path. Authentication MUST run again for the new connection.

#### Scenario: Reconnect after a torn-down failed scan starts clean

- **WHEN** a legacy failed scan tears down the privileged connection and a later connection becomes Active
- **THEN** the engine MUST obtain new connection-scoped volume identities
- **AND** MUST subject that connection to a fresh authorization decision
- **AND** MUST NOT apply the prior connection's in-flight scan authority
