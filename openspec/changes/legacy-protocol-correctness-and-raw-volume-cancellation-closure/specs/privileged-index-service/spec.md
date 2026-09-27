## ADDED Requirements

### Requirement: Failed scans are never successful terminals because ScanFailed is unsupported

The privileged service MUST NOT emit `ScanComplete` for a scan that did not run to completion, including when the negotiated peer cannot parse `ScanFailed`. The service MUST NOT send `ScanFailed` to a peer that cannot parse it. Cancellation and transport failure remain silent (no terminal). Genuine end of enumeration remains `ScanComplete` for every supported version. The closed command allowlist MUST remain closed; no generic path/handle-open primitive may be added.

#### Scenario: Pre-ScanFailed peer receives no success terminal on failure

- **WHEN** a connected engine negotiated a protocol version that does not support `ScanFailed` and a volume scan fails before or during enumeration
- **THEN** the service MUST NOT send `ScanComplete` or `ScanFailed` for that scan
- **AND** MUST end that attempt so the peer cannot treat it as a completed enumeration

#### Scenario: ScanFailed-capable peer still receives ScanFailed

- **WHEN** the negotiated version supports `ScanFailed` and the scan cannot complete
- **THEN** the service MUST send `ScanFailed` instead of `ScanComplete`

### Requirement: Stop and teardown request cancellation without claiming kernel completion

`StopVolumeScan`, superseding a live scan, connection disconnect, and service shutdown MUST request worker stop and cancellation of in-flight synchronous I/O. Those requests MUST NOT be specified as a finite completion deadline for an arbitrary raw-volume `DeviceIoControl`. Empty-volume scan and journal responsiveness required elsewhere in this capability remain unchanged: a volume with no data currently to report MUST still be acknowledged without waiting indefinitely for data to appear.

#### Scenario: Stop requests cancellation then joins the owned worker

- **WHEN** the owning connection sends `StopVolumeScan` for a connection-scoped volume whose worker may be inside a synchronous raw-volume operation
- **THEN** the service MUST request that worker to stop and MUST request cancellation of its synchronous I/O
- **AND** MUST NOT describe that request as proof the kernel operation finished

#### Scenario: Disconnect still tears down owned scans and journals

- **WHEN** a connection closes while it has open scans or journals
- **THEN** the service MUST tear down that connection's scans and journals without requiring an explicit `Stop`/`Close`
- **AND** MUST NOT leave those workers able to write to a later connection
