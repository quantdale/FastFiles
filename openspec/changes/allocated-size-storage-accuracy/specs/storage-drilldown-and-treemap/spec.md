# storage-drilldown-and-treemap Specification (allocated-size delta)

Change-local additive requirements for `allocated-size-storage-accuracy`.
These are staged here and synchronized into the canonical
`openspec/specs/storage-drilldown-and-treemap/spec.md` when the change
completes.

## Purpose
Surfaces allocated (on-disk) size wherever logical size is shown, with honest
unknown and partial-aggregate rendering, while keeping treemap area
logical-size-driven (storage-analysis design D5).

## Requirements

### Requirement: Drill-Down Shows Logical and Allocated Size

The drill-down listing SHALL expose each item's logical size and, when
known, its allocated (on-disk) size. Unknown allocated size SHALL be rendered
as an explicit unknown affordance, never as `0 B` and never by substituting
the logical size.

#### Scenario: Known allocated size is displayed exactly
- **WHEN** a file's allocated size is known
- **THEN** the listing SHALL display both `Size` (logical) and
  `Size on disk` (allocated) with distinct labels

#### Scenario: Unknown allocated size is honest
- **WHEN** a file's allocated size is not known (for example, degraded mode)
- **THEN** the listing SHALL display an unknown affordance for size on disk,
  and the logical size column SHALL be unaffected

### Requirement: Folder Aggregates Distinguish Complete, Partial, and Unknown

Folder aggregates SHALL show both the logical total and the allocated
(on-disk) total. A complete allocated total SHALL be shown exactly; a partial
one SHALL be shown with an explicit qualifier (e.g. `≥` with a known/total
indication); an all-unknown aggregate SHALL be shown as unknown.

#### Scenario: Complete aggregate is exact
- **WHEN** every descendant's allocated size is known
- **THEN** the folder's size-on-disk figure SHALL be the exact aggregate

#### Scenario: Partial aggregate is labeled, not silently exact
- **WHEN** only a subset of descendants have known allocated size
- **THEN** the folder's size-on-disk figure SHALL be presented as a lower
  bound with the known/total qualifier, never as an exact total

### Requirement: Treemap Hover Shows Both Values with Divergence Callout

Treemap hover metadata SHALL show logical size and, when known, allocated
size together. When the two known values diverge materially, the presentation
SHALL make the distinction obvious (for example a sparse/compressed
qualifier). Rectangle area SHALL remain driven by logical size; no metric
toggle is introduced by this change.

#### Scenario: Divergence is called out
- **WHEN** a hovered item's known allocated size differs materially from its
  logical size
- **THEN** the hover SHALL display both values and SHALL call out the
  divergence explicitly

#### Scenario: Unknown allocated size in hover
- **WHEN** a hovered item's allocated size is unknown
- **THEN** the hover SHALL show logical size and an unknown affordance for
  size on disk

### Requirement: No Destructive or Blocking Behavior Added

The size-on-disk presentation SHALL add no filesystem mutation, no per-item
filesystem probing in degraded mode, no UI-thread traversal, and no blocking
of navigation.

#### Scenario: Degraded mode remains non-blocking
- **WHEN** the privileged service is unavailable
- **THEN** drill-down and treemap SHALL continue to open and navigate
  exactly as before, with size-on-disk shown as unknown
