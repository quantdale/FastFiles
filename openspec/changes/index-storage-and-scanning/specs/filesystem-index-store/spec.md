## ADDED Requirements

### Requirement: Durable SQLite-Backed Persistence
`FastFilesEngine` SHALL persist the authoritative filesystem index in a single-file SQLite database opened in WAL (write-ahead log) journal mode, as the crash-safe source of truth all other index representations are rebuilt from.

#### Scenario: Index survives an unclean engine shutdown
- **WHEN** `FastFilesEngine` terminates abnormally (crash, forced kill, power loss) while the durable store has committed transactions pending checkpoint
- **THEN** the SQLite database SHALL be recoverable on next open via WAL replay, without requiring the index to be rebuilt from a full rescan

#### Scenario: Corruption is detectable rather than silently trusted
- **WHEN** the engine opens the durable store at startup
- **THEN** the engine SHALL be able to run an integrity check against the database and treat a failed check as requiring the store to be rebuilt from a fresh volume scan, rather than operating on data known to be corrupt

### Requirement: Batched Transactional Writes Under WAL
Ingestion of scanned or streamed records into the durable store SHALL be performed as explicit, multi-record transactions (batched commits), not one transaction per individual record, and SHALL permit concurrent readers (reconciliation sweeps, diagnostics) to proceed without being blocked by an in-progress ingestion transaction.

#### Scenario: A batch of streamed records commits as one transaction
- **WHEN** the engine receives a batch of records from an active scan or journal stream
- **THEN** the engine SHALL write the entire batch within a single database transaction rather than committing each record individually

#### Scenario: A concurrent read is not blocked by an in-progress write transaction
- **WHEN** a reconciliation sweep or diagnostic read is in progress against the durable store
- **THEN** an ingestion write transaction committing during that time SHALL NOT be blocked by, and SHALL NOT block, that concurrent read

### Requirement: Entry Identity via Volume Identifier and File Reference Number
Every persisted entry SHALL be uniquely keyed by the pair (durable volume identifier, `FileReferenceNumber`), and this pair SHALL be the only identity used for insert, update, and lookup operations. No persisted entry SHALL be keyed or deduplicated by path string.

#### Scenario: Re-ingesting the same physical file updates one row, not two
- **WHEN** a record for a `FileReferenceNumber` already present for a given volume is ingested again (e.g., re-observed during a reconciliation sweep or a resumed scan)
- **THEN** the store SHALL update the existing row for that (volume identifier, `FileReferenceNumber`) pair rather than inserting a duplicate row

#### Scenario: Identical paths on different volumes never collide
- **WHEN** two different volumes each contain an entry that would resolve to the same path string
- **THEN** the store SHALL treat them as entirely distinct entries, since their (volume identifier, `FileReferenceNumber`) pairs differ

### Requirement: Compact In-Memory Projection
`FastFilesEngine` SHALL maintain an in-memory projection of the index in which each entry is a fixed-size record referencing its parent by `FileReferenceNumber` (or an equivalent dense internal reference) and its name by an interned/deduplicated string id, rather than by a stored full path string. This projection, not the durable store, is what is published to UI clients per the `index-engine` capability's snapshot-publication mechanism.

#### Scenario: Sibling entries with long shared ancestor paths do not duplicate that path in memory
- **WHEN** the projection holds many entries under a common, deeply-nested ancestor directory
- **THEN** the shared ancestor path SHALL NOT be stored redundantly per entry; each entry SHALL reference its parent rather than storing a materialized path

#### Scenario: Repeated file/directory names across the tree share one interned string
- **WHEN** the same name (e.g., a common filename or directory name) occurs under many different parents across the indexed volume
- **THEN** the projection SHALL store that name once in its interned string pool and reference it by id from every entry with that name, rather than storing the string once per entry

#### Scenario: A full path is reconstructed only when needed
- **WHEN** a UI client or engine-internal consumer needs an entry's full path (e.g., for display or to open the file)
- **THEN** the engine SHALL reconstruct it by walking parent references and concatenating interned names on demand, rather than reading a precomputed path field

### Requirement: Startup Projection Rebuild from the Durable Store
On `FastFilesEngine` startup, the in-memory projection SHALL be rebuilt entirely from the durable store's current contents before being published, and this rebuild SHALL NOT depend on any data other than what the durable store persisted.

#### Scenario: Projection after restart matches the durable store, not stale in-memory state
- **WHEN** `FastFilesEngine` restarts after a prior run
- **THEN** the rebuilt in-memory projection SHALL reflect exactly what the durable store contains at that moment, with no dependency on the previous process's in-memory state

#### Scenario: A volume's rebuild is published as soon as it completes
- **WHEN** multiple volumes' data must be rebuilt into the projection at startup
- **THEN** the engine SHALL publish each volume's rebuilt data as its own snapshot generation as soon as that volume's rebuild completes, rather than withholding publication until every volume has finished rebuilding

### Requirement: Incremental Projection Synchronization
After startup, every batch of records ingested into the durable store SHALL be applied to the in-memory projection only after the corresponding durable-store transaction has committed, keeping the projection and the durable store consistent without requiring a rebuild for every update.

#### Scenario: Projection update follows durable commit, never precedes it
- **WHEN** a batch of records is being ingested
- **THEN** the projection SHALL be updated only after the durable store's transaction for that batch has committed successfully

#### Scenario: A crash between commit and projection update self-heals on restart
- **WHEN** the engine crashes after a batch has committed to the durable store but before that batch was applied to the in-memory projection
- **THEN** the next startup's projection rebuild (sourced from the durable store) SHALL include that batch's data, so no committed data is permanently missing from the live view

### Requirement: Symlink, Junction, and Reparse-Point Cycle Safety
Because entries are identified solely by (volume identifier, `FileReferenceNumber`) rather than by path, the store SHALL be immune to infinite traversal or double-counting caused by junctions, symlinks, or other reparse points that create apparent path cycles.

#### Scenario: A junction pointing back into its own ancestry does not cause infinite growth
- **WHEN** the indexed volume contains a junction or symlink whose target is an ancestor of the junction itself
- **THEN** the store SHALL contain exactly one entry for each physical MFT record involved, and ingestion SHALL terminate normally rather than recursing indefinitely

#### Scenario: A parent-chain walk defensively detects a cycle
- **WHEN** reconstructing a full path by walking parent references (per the in-memory projection's on-demand path reconstruction)
- **THEN** the walk SHALL detect if it revisits a `FileReferenceNumber` already seen in that same walk and SHALL stop rather than looping indefinitely, even though a well-formed volume should never produce such a parent cycle

### Requirement: Resumable Scan Progress
The store SHALL persist a per-volume scan-progress cursor during an in-progress initial volume scan, updated only after the batch it corresponds to has committed, so that an interrupted scan can resume from approximately where it left off rather than restarting from the beginning.

#### Scenario: An interrupted scan's progress is not lost
- **WHEN** `FastFilesEngine` is closed or crashes while a volume's initial scan is still in progress
- **THEN** the store SHALL retain both the entries already committed and a scan-progress cursor reflecting how far the scan had reached

#### Scenario: Resuming a scan is safe even if the cursor is approximate
- **WHEN** a resumed scan re-delivers a record for a `FileReferenceNumber` already committed during the prior, interrupted attempt
- **THEN** the store SHALL treat this as a harmless update to the existing (volume identifier, `FileReferenceNumber`) row rather than a duplicate or an error, per the entry-identity requirement

### Requirement: Disconnected or Removed Volume Retention Without Corruption
When a previously-indexed volume becomes unavailable (removed, unmounted, or otherwise no longer enumerable), the store SHALL retain that volume's entries and mark the volume's status as unavailable, and SHALL NOT delete the volume's entries or allow this transition to affect any other volume's data.

#### Scenario: A removed external drive's entries remain queryable but marked unavailable
- **WHEN** a previously-indexed removable volume is disconnected
- **THEN** its entries SHALL remain present in the durable store and in-memory projection, marked as belonging to an unavailable volume, and SHALL be excluded from active/live results unless explicitly requested

#### Scenario: One volume's disconnection does not corrupt or affect other volumes
- **WHEN** one indexed volume becomes unavailable
- **THEN** entries belonging to all other volumes SHALL remain unaffected, and no store-wide operation SHALL be triggered that risks their integrity

#### Scenario: Volume data is only removed by explicit action
- **WHEN** a volume has been marked unavailable
- **THEN** its entries SHALL remain in the store indefinitely unless a user explicitly requests that volume's index data be forgotten/removed

### Requirement: Volume Reconnection Triggers Resume-or-Reconcile, Not Blind Rescan
When a previously-unavailable volume becomes available again, the store's volume-status data SHALL be used to determine whether the persisted USN journal position can be resumed or whether a reconciliation sweep is required, rather than unconditionally discarding and re-scanning the volume's entries from zero.

#### Scenario: Matching journal identity resumes incrementally
- **WHEN** a reconnected volume reports the same USN journal identifier that was persisted for it
- **THEN** the store's persisted resume position SHALL be used to continue incremental journal ingestion for that volume

#### Scenario: Mismatched or invalid journal identity triggers reconciliation instead of full rescan
- **WHEN** a reconnected volume reports a different journal identifier than the one persisted, or the persisted resume position is no longer within the journal's retained range
- **THEN** the store SHALL mark that volume as requiring a reconciliation sweep rather than resuming incrementally, and existing entries whose underlying files are unchanged SHALL be reused (updated in place) rather than deleted and re-created

### Requirement: Periodic Reconciliation Sweeps
The store SHALL support periodic, per-volume reconciliation sweeps that compare persisted entries against current ground truth and correct drift — removing entries with no on-disk counterpart, adding entries missing from the index, and updating stale fields — without requiring a full manual rescan to be triggered by the user.

#### Scenario: A missed delete event is corrected by the next sweep
- **WHEN** a file was deleted while no journal watch was active for its volume (e.g., during application downtime) and no corresponding change record was ever ingested
- **THEN** the next reconciliation sweep for that volume SHALL detect the entry no longer resolves and remove it from the index

#### Scenario: A missed create event is corrected by the next sweep
- **WHEN** a file was created while no journal watch was active for its volume and no corresponding change record was ever ingested
- **THEN** the next reconciliation sweep for that volume SHALL detect the new file and add it to the index

#### Scenario: Reconciliation does not require full manual rescan
- **WHEN** a reconciliation sweep runs
- **THEN** it SHALL correct drift for the volume being swept without deleting and fully re-populating that volume's entire set of entries from scratch
