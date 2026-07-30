## MODIFIED Requirements

### Requirement: Filesystem Snapshot Publication
`FastFilesEngine` SHALL publish its current view of the filesystem index to UI clients as an immutable, read-only snapshot accessible via a shared memory mapping, and SHALL notify subscribed clients of new snapshot generations over a control IPC channel, rather than requiring clients to make a round-trip IPC call to read already-published data. This view SHALL extend to the full persisted, incrementally-updated index maintained by the `filesystem-index-store` capability — every entry known to the durable store and its in-memory projection, across all indexed volumes — not only directories the user has actively browsed or pinned, which was this requirement's scope prior to this change (the foundation change's degraded-mode-only scope).

#### Scenario: UI reads already-published data without an IPC round trip
- **WHEN** a UI client has mapped the current snapshot generation
- **THEN** reading the contents of a directory already reflected in that snapshot SHALL require no IPC call to the engine

#### Scenario: New snapshot generation is announced
- **WHEN** the engine's view of the index changes (new entry, removed entry, changed entry, or an initial enumeration/scan completing)
- **THEN** the engine SHALL publish a new snapshot generation and notify subscribed UI clients of it over the control pipe

#### Scenario: Snapshot reflects whole-volume index data, not only browsed directories
- **WHEN** the privileged path is `Active` and a volume's initial scan has completed or is progressing
- **THEN** the published snapshot SHALL include entries the user has never browsed to, sourced from the persisted index rather than from on-demand directory enumeration

#### Scenario: Degraded mode still publishes a snapshot scoped to browsed directories
- **WHEN** the engine is not in the `Active` privileged state
- **THEN** the published snapshot SHALL continue to reflect at least the directories the user has browsed or pinned, consistent with degraded-mode operation, even though whole-volume coverage is unavailable in that state

## ADDED Requirements

### Requirement: Ingestion of Privileged Scan and Journal Streams
While in the `Active` state, `FastFilesEngine` SHALL ingest batched records received from `StartVolumeScan` and `OpenUsnJournal` by persisting each batch to the `filesystem-index-store` capability's durable store and then applying the same batch to the in-memory projection, in that order, before considering the batch fully ingested.

#### Scenario: A streamed batch is durably persisted before it affects the live view
- **WHEN** the engine receives a batch of records from an open scan or journal stream
- **THEN** the engine SHALL commit that batch to the durable store first, and only apply it to the in-memory projection (and publish a new snapshot generation) after that commit succeeds

#### Scenario: A failed persistence attempt does not corrupt the live view
- **WHEN** persisting a received batch to the durable store fails
- **THEN** the engine SHALL NOT apply that batch to the in-memory projection, and SHALL retry persistence rather than allowing the live view to diverge from the durable store

### Requirement: Scan Resumption After Restart
On startup, before issuing a fresh `StartVolumeScan` for a volume, `FastFilesEngine` SHALL check the `filesystem-index-store` capability for a persisted scan-progress cursor for that volume, and if one exists, SHALL supply it to `StartVolumeScan` so the privileged service resumes enumeration rather than restarting from the beginning.

#### Scenario: Restart after an interrupted scan resumes instead of restarting
- **WHEN** `FastFilesEngine` starts and finds a persisted scan-progress cursor for a volume whose initial scan had not completed before the previous session ended
- **THEN** the engine SHALL call `StartVolumeScan` for that volume with the persisted cursor rather than omitting it

### Requirement: Volume Availability Tracking and Reconnection Handling
`FastFilesEngine` SHALL determine volume availability from each `EnumerateVolumes` response and reflect disappearance and reappearance of a volume in the `filesystem-index-store` capability's volume-status data, rather than leaving stale entries silently marked as available or discarding them.

#### Scenario: A volume no longer enumerated is marked unavailable, not deleted
- **WHEN** a previously-enumerated volume is absent from a subsequent `EnumerateVolumes` response
- **THEN** the engine SHALL mark that volume's persisted status as unavailable and SHALL NOT delete its entries from the durable store or the in-memory projection

#### Scenario: A reappearing volume triggers resume-or-reconcile, not an unconditional rescan
- **WHEN** a volume previously marked unavailable is enumerated again
- **THEN** the engine SHALL attempt to resume its USN journal from the persisted position if the reported journal identity still matches, and SHALL otherwise trigger a reconciliation sweep for that volume rather than unconditionally issuing a full rescan

### Requirement: Engine-Driven Reconciliation Scheduling
`FastFilesEngine` SHALL schedule periodic reconciliation sweeps (via the `filesystem-index-store` capability) for each available volume, independent of and in addition to incremental scan/journal ingestion, so the index self-corrects from missed events or application downtime without requiring the user to trigger a manual rescan.

#### Scenario: A sweep runs without user action after a period of missed events
- **WHEN** the configured reconciliation interval elapses for an available volume
- **THEN** the engine SHALL initiate a reconciliation sweep for that volume without requiring any user-initiated rescan action

#### Scenario: Reconciliation scheduling does not require the privileged path
- **WHEN** the engine is operating in degraded mode (no active privileged connection)
- **THEN** the engine SHALL NOT schedule privileged-path reconciliation sweeps for volumes that depend on the privileged connection, and SHALL resume normal reconciliation scheduling once the privileged path returns to `Active`
