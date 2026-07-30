## MODIFIED Requirements

### Requirement: Real MFT Volume Scanning and USN Journal Streaming
`FastFilesIndexSvc` SHALL perform real raw MFT parsing in response to `StartVolumeScan` and real USN Change Journal reads in response to `OpenUsnJournal`, streaming batched records to the requesting connection, rather than the "not yet implemented" stub response defined in `establish-architecture-foundation`. This requirement supersedes and replaces that change's "Scan and Journal Operations Are Explicitly Stubbed" requirement. Both operations SHALL continue to respond within the service's normal response time and SHALL NOT hang or leave the requesting connection waiting indefinitely, and SHALL parse and forward only the allowlisted fields already established (`$STANDARD_INFORMATION`, `$FILE_NAME` — never `$DATA` content).

#### Scenario: Scan request streams real MFT-derived records
- **WHEN** a client sends `StartVolumeScan` for a valid, service-enumerated `VolumeId`
- **THEN** the service SHALL open the volume using its `SeBackupPrivilege`-granted raw access, read MFT records directly, and stream batches of allowlisted-field records (`FileReferenceNumber`, `ParentFileReferenceNumber`, name, size, timestamps, attributes) to the requesting connection until the scan completes or is stopped

#### Scenario: Journal open streams real change records from a given resume point
- **WHEN** a client sends `OpenUsnJournal` for a valid `VolumeId` with a `ResumeUsn`
- **THEN** the service SHALL read the volume's USN Change Journal starting at that position (via `FSCTL_QUERY_USN_JOURNAL`/`FSCTL_READ_USN_JOURNAL`) and stream batches of allowlisted-field change records to the requesting connection as they occur

#### Scenario: Scanning proceeds despite files locked by other processes
- **WHEN** a raw MFT scan encounters a file or directory that is exclusively locked by another process
- **THEN** the service's `SeBackupPrivilege`-based raw volume access SHALL still be able to read that record's allowlisted metadata, since raw MFT reads do not require opening the file through the normal sharing-mode path

#### Scenario: Scan and journal calls remain responsive when the volume has no data yet to report
- **WHEN** `StartVolumeScan` or `OpenUsnJournal` is called on a volume with no new records currently available
- **THEN** the service SHALL respond within its normal response time (an empty or pending-batch acknowledgment), never blocking the connection indefinitely waiting for data to appear

## ADDED Requirements

### Requirement: MFT Attribute Allowlisting Enforced at the Parser
The MFT parser SHALL read and forward only the allowlisted attribute types (`$STANDARD_INFORMATION`, `$FILE_NAME`) for each record. Any other attribute type present in a record, including `$DATA`, SHALL be ignored and SHALL NOT be forwarded to the requesting connection under any circumstance, including for small files whose content is resident inside the MFT record itself.

#### Scenario: Resident file content is never forwarded
- **WHEN** a scanned MFT record has resident `$DATA` content stored inline (a small file)
- **THEN** the service SHALL forward only that record's `$STANDARD_INFORMATION`/`$FILE_NAME` fields and SHALL NOT include any `$DATA` bytes in the streamed record

#### Scenario: Non-allowlisted attribute type is dropped, not forwarded
- **WHEN** an MFT record contains an attribute type outside the allowlist (e.g., `$SECURITY_DESCRIPTOR`, `$REPARSE_POINT` payload bytes, alternate data streams)
- **THEN** the service SHALL exclude that attribute's content entirely from the streamed record without treating its presence as an error

### Requirement: Malformed MFT or USN Records Are Isolated, Not Fatal
A single malformed or internally inconsistent MFT record or USN change record encountered mid-scan or mid-journal-read SHALL be skipped and logged, and SHALL NOT abort the remainder of the scan or journal stream, provided the record itself passes the protocol-level frame and length validation already required. This is distinct from frame-level validation (which governs wire-format integrity); this requirement governs semantic validity of an individual on-disk NTFS record once its frame has already been accepted.

#### Scenario: One corrupt record does not stop the scan
- **WHEN** the service encounters an MFT record with internally inconsistent fields (e.g., a `$FILE_NAME` attribute whose declared name length does not fit within the record) while scanning a volume
- **THEN** the service SHALL skip that single record, continue scanning subsequent records, and SHALL NOT close the connection or abort the scan solely because of that one record

### Requirement: USN Journal Identity Reported for Resume Validation
`OpenUsnJournal` responses SHALL include the volume's current USN Journal identifier (`JournalId`) alongside streamed records, so the caller can detect when a previously-persisted `ResumeUsn` is no longer valid for the journal now present on the volume.

#### Scenario: Journal identity is included so the caller can detect a recreated journal
- **WHEN** a client calls `OpenUsnJournal` with a `ResumeUsn` obtained from a previous session
- **THEN** the service SHALL report the volume's current `JournalId` to the caller, allowing the caller to determine whether that `ResumeUsn` still applies to the same journal instance or belongs to a journal that has since been deleted and recreated

### Requirement: Scan Resumption from a Caller-Supplied Cursor
`StartVolumeScan` SHALL accept an optional, opaque scan-progress cursor previously issued by the service, and SHALL resume MFT enumeration from approximately that point rather than always restarting from the beginning of the MFT, as an additive extension to the existing closed command surface.

#### Scenario: Scan resumes from a supplied cursor instead of record zero
- **WHEN** a client calls `StartVolumeScan` for a `VolumeId` and supplies a previously-issued scan-progress cursor
- **THEN** the service SHALL resume MFT enumeration from approximately the position that cursor represents rather than re-enumerating the volume's MFT from its start

#### Scenario: Omitted cursor performs a full scan
- **WHEN** a client calls `StartVolumeScan` without supplying a scan-progress cursor
- **THEN** the service SHALL perform a full MFT enumeration from the beginning, exactly as it would for a first-time scan of that volume
