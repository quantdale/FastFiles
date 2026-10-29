# index-engine (delta): bulk removal and filename-accelerator eligibility

## ADDED Requirements

### Requirement: Bulk removal shall replace per-entry accelerator maintenance for large removals

The projection SHALL remove large row batches (more than
`kPerEntryFilenameRemovalThreshold` rows) with one wholesale filename-accelerator
invalidation and at most one bulk rebuild, instead of per-entry posting removal.
Observable removal semantics (rows, parent/child links, folder aggregates, path
reconstruction, name ownership, LRU slots) SHALL be identical to per-entry removal.
Small batches SHALL keep the per-entry path so the accelerator stays current
without a full rebuild. Callers that rebuild the accelerator as part of an
enclosing bulk operation SHALL be able to skip the redundant rebuild.

#### Scenario: Replacement rebuild cost is at first-build order

- **WHEN** a populated projection is rebuilt from the durable store over identical rows
- **THEN** observable state matches a fresh rebuild exactly
- **AND** removal no longer performs per-entry trigram posting erases

#### Scenario: Forget-volume and reconciliation mass removal

- **WHEN** a volume is forgotten, a reconciliation pass removes unseen rows, or rules
  prune excluded rows in bulk
- **THEN** the projection applies one bulk accelerator update
- **AND** the accelerator is current (or deliberately deferred to an enclosing rebuild)
  afterwards

### Requirement: Filename-accelerator candidates shall not be inflated by unmatchable rows

For a pure-ASCII folded needle of at least three units whose trigram posting
intersection is within the caller's candidate cap, the accelerator SHALL return
that intersection as the candidate set. Rows whose names contain no ASCII trigram
occurrence of the needle SHALL NOT be force-included in the candidate set merely
because their names contain non-ASCII characters: `FoldAscii` is length-preserving
and non-ASCII names' folded trigrams are indexed, so the posting intersection is
already a conservative superset of every fold-matching or ordinally-matching row.
Needles shorter than three folded units, non-ASCII folded needles, and lookups
whose candidate set would exceed the cap SHALL return `nullopt` (canonical scan),
and queries whose smallest posting already exceeds the cap SHALL remain on the
canonical scan (no forced index use for broad queries).

#### Scenario: Selective needle with a Unicode-majority projection

- **WHEN** a selective pure-ASCII needle is looked up in a projection whose
  non-ASCII-named rows exceed the candidate cap
- **THEN** a bounded candidate set is returned containing exactly the fold-matching rows
- **AND** the accelerator remains usable at 200k/1M/2M-row scale

#### Scenario: Conservative guarantee across name shapes

- **WHEN** ASCII names, Unicode names containing ASCII runs, mixed-case ASCII runs,
  dotted filenames, and short names are queried with pure-ASCII needles
- **THEN** every row the canonical matcher can match is present in the candidate set
- **AND** end-to-end results (identities, ordering, caps) match the canonical path
  exactly across sort fields and directions
