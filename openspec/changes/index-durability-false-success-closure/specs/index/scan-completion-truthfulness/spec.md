# Spec: Scan Completion Truthfulness

## Purpose

Guarantee that the durable index only treats scan/reconciliation completion as
authoritative when the enumeration actually ran to completion and every record
was applied, that terminal scan failure is reported distinctly from success, that
a reconciliation pass can only be finalized by its own authoritative scan, and
that a transient (non-corruption) startup integrity fault never destroys the
persistent index.

## ADDED Requirements

### Requirement: A scan that cannot run to completion reports failure, not completion

The privileged service MUST report a scan that fails before or during
enumeration with a distinct terminal failure outcome (`ScanFailed`) rather than
the success outcome (`ScanComplete`), for peers that negotiated a protocol
version supporting the failure message. Genuine end of enumeration
(`ERROR_HANDLE_EOF`) remains a success completion. Cancellation and transport
failure remain silent (no terminal message).

#### Scenario: Raw-volume open denial reports failure

- **WHEN** a scan begins but the raw volume device cannot be opened, the backup
  privilege cannot be enabled, `FSCTL_GET_NTFS_VOLUME_DATA` fails, or the volume
  geometry is degenerate
- **THEN** the service MUST send `ScanFailed` for that volume and MUST NOT send
  `ScanComplete`
- **AND** the engine MUST NOT mark the volume's scan complete

#### Scenario: Mid-enumeration I/O fault reports failure

- **WHEN** an `FSCTL_ENUM_USN_DATA` call fails with any error other than
  `ERROR_HANDLE_EOF` while records are being enumerated
- **THEN** the scan MUST terminate with `ScanFailed`, not `ScanComplete`

#### Scenario: Normal enumeration end still reports completion

- **WHEN** enumeration ends because `FSCTL_ENUM_USN_DATA` returns
  `ERROR_HANDLE_EOF`
- **THEN** the service MUST send `ScanComplete` exactly as before

#### Scenario: Legacy peers keep current behavior

- **WHEN** the peer negotiated a protocol version that does not support the
  failure message
- **THEN** the service MUST NOT send `ScanFailed` and the wire bytes a legacy
  peer receives for a successful scan are unchanged

### Requirement: Reconciliation finalization requires its own authoritative scan

`FinishReconciliationPass` (and the reconciliation-clock advance) MUST be reached
only on a completion that is (a) for the in-flight scan, (b) for the reconciliation
pass's own scan, (c) not paused, and (d) had every batch applied. A completion
that does not meet these conditions MUST NOT remove durable entries.

#### Scenario: Failed scan aborts an active reconciliation pass

- **WHEN** `ScanFailed` arrives while a reconciliation pass is active for that
  volume
- **THEN** the engine MUST abort the pass without computing or removing any
  durable entries, and MUST NOT mark the scan complete or advance the
  reconciliation clock

#### Scenario: Batch-apply failure makes completion non-authoritative

- **WHEN** one scan batch's durable apply fails during a scan and the scan later
  reports `ScanComplete`
- **THEN** the engine MUST NOT mark the volume scan complete, and MUST abort any
  active reconciliation pass rather than finalize it on incomplete evidence

#### Scenario: Duplicate or stale completion is ignored

- **WHEN** a `ScanComplete` arrives for a volume that has no scan in flight
- **THEN** the engine MUST ignore it (with a diagnostic) and MUST NOT mark the
  volume complete or finalize a reconciliation pass

### Requirement: Reconciliation requested during an in-flight scan is deferred

A reconciliation request that arrives while a scan (initial or reconciliation) is
already in flight for the volume MUST be deferred, and MUST NOT begin a new pass
or clear an in-progress pass's observed set mid-scan. Deferred reconciliation
runs at the next authoritative completion.

#### Scenario: Journal-resume-invalid during the initial scan

- **WHEN** `OnJournalResumeInvalid` (or a JournalId mismatch at journal open)
  occurs while the volume's initial scan is still running
- **THEN** the engine MUST defer reconciliation until the initial scan's
  authoritative completion, and MUST NOT clear the running scan's reconciliation
  observed set

### Requirement: Journal-visible entries survive a reconciliation pass

Entries upserted by the live USN journal while a reconciliation pass is active,
and which pass the volume's inclusion rules, MUST be recorded as observed so they
are not removed at pass finalization.

#### Scenario: File created during the pass is retained

- **WHEN** a reconciliation pass is active, the journal upserts a new entry that
  satisfies the volume rules, and the pass later finalizes on an authoritative
  completion that did not include that entry in its MFT batch
- **THEN** the entry MUST remain present in the durable store and the projection

### Requirement: A transient startup integrity fault never deletes the index

The store MUST classify startup integrity verification as success, genuine
corruption, or inconclusive. Only genuine corruption may trigger the
delete-and-recreate fallback. An inconclusive result (PRAGMA prepare failure, or
a step result that is a non-corruption error such as BUSY/LOCKED/IOERR/NOMEM/FULL)
MUST fail the open without deleting the database, its WAL, or its SHM.

#### Scenario: Corruption still triggers the documented rebuild fallback

- **WHEN** integrity verification returns a genuine corruption verdict
- **THEN** the engine MUST delete `index.db` + `-wal` + `-shm` and reopen a fresh
  store, as before

#### Scenario: Transient fault preserves the database

- **WHEN** integrity verification returns inconclusive (for example the step
  returns `SQLITE_BUSY` or the PRAGMA statement cannot be prepared)
- **THEN** `Store::Open` MUST fail with `outIntegrityFailed == false`, the engine
  MUST NOT delete any index file, and it MUST fall back to the existing
  degraded/no-durable-persistence path (optionally after a small bounded retry)
