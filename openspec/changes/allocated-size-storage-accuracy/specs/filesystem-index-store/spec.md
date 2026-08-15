# filesystem-index-store Specification (allocated-size delta)

Change-local additive requirements for `allocated-size-storage-accuracy`.
These are staged here and synchronized into the canonical
`openspec/specs/filesystem-index-store/spec.md` when the change completes.

## Purpose
Completes the index's size model: each entry carries both logical size and
allocated (on-disk) size, with an explicit unknown state, through scan,
journal, durable store, in-memory projection, snapshot, and folder
aggregates.

## Requirements

### Requirement: Per-Entry Logical and Allocated Size with Explicit Unknown

Every indexed entry SHALL carry a logical size (`sizeBytes`, the
`$FILE_NAME` `RealSize`) and, when known, an allocated (on-disk) size
(`allocatedSizeBytes`, the `$FILE_NAME` `AllocatedSize`) derived from the
allowlisted `$STANDARD_INFORMATION`/`$FILE_NAME` metadata only. Unknown
allocated size SHALL be represented explicitly (presence mask on the wire,
`NULL` in the durable store, an explicit known flag in the projection and
snapshot) and SHALL be distinguishable from a genuine zero-byte value.

#### Scenario: A full MFT scan populates both sizes
- **WHEN** the privileged service parses a `$FILE_NAME` attribute during a
  volume scan or reconciliation sweep
- **THEN** the forwarded record SHALL carry both the logical size and the
  allocated size read from that same validated attribute, and the engine
  SHALL persist and project both values

#### Scenario: A zero-byte file is stored as zero, not unknown
- **WHEN** an entry's parsed `AllocatedSize` is zero
- **THEN** it SHALL be stored as a known zero, and SHALL remain
  distinguishable from an entry whose allocated size was never observed

#### Scenario: $DATA is never consulted for either size
- **WHEN** the parser produces logical/allocated size
- **THEN** no `$DATA` attribute (resident or non-resident) SHALL be opened,
  parsed, or forwarded in the process

### Requirement: Incremental Updates Preserve Unobserved Allocated Size

An incremental USN update whose allocated size was not obtained (for
example, when MFT re-enrichment fails) SHALL NOT erase a previously known
allocated size with zero; the prior value and known state SHALL be retained.

#### Scenario: Re-enrichment failure does not destroy known values
- **WHEN** a USN delta for an existing entry arrives without the allocated
  size field present
- **THEN** the durable row and projection entry SHALL retain their previous
  allocated size and known state

#### Scenario: Reconciliation corrects stale allocated size
- **WHEN** a reconciliation sweep re-observes an entry whose on-disk
  allocated size changed
- **THEN** the corrected value SHALL replace the stale one, including
  transitioning an unknown value to known

### Requirement: Durable Schema Migration Is Additive

The durable store SHALL migrate in place from schema version 1 to version 2
by adding a nullable `allocated_size_bytes` column, without requiring
existing databases to be deleted or rebuilt. Rows whose allocated size
predates the feature SHALL read back as unknown, not as zero.

#### Scenario: Existing database upgrades in place
- **WHEN** a version-1 database is opened by the current store
- **THEN** the schema SHALL be upgraded in place to version 2, all existing
  rows SHALL remain, and rows without an allocated size SHALL be reported as
  unknown

#### Scenario: Migration failure leaves the database usable
- **WHEN** the migration cannot be applied
- **THEN** the store SHALL fail closed (refuse to open) rather than operate
  with an ambiguous schema, and the existing integrity-check/rebuild
  fallback SHALL apply

### Requirement: Folder Aggregates Report Both Totals with Completeness

A folder aggregate SHALL report the item count, the logical-size total, the
allocated-size subtotal over descendants whose allocated size is known, and
an explicit completeness pair (known count / total count). Arithmetic SHALL
be checked, saturating at `UINT64_MAX` with the aggregate marked incomplete
on saturation, so a partial subtotal is never presented as an exact total.

#### Scenario: All descendants known yields a complete allocated total
- **WHEN** every descendant of a folder has a known allocated size
- **THEN** the aggregate SHALL report the exact allocated total and mark the
  allocated portion complete

#### Scenario: Unknown descendants yield an explicit partial aggregate
- **WHEN** some descendants' allocated size is unknown
- **THEN** the aggregate SHALL report the known subtotal plus the
  known/total counts, and SHALL NOT claim the subtotal is the folder's full
  on-disk footprint
