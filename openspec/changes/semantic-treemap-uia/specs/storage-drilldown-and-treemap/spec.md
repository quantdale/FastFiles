# semantic-treemap-uia — storage-drilldown-and-treemap delta

## Purpose

Delta for `semantic-treemap-uia`: corrects the product behavior where the
current implementation contradicts the canonical
`storage-drilldown-and-treemap` spec, and makes the percentage and
small-item contracts explicit. No canonical requirement is weakened; the
delta records what the audit exposed and what the corrective change
establishes.

## ADDED Requirements

### Requirement: Drill-down listing shows percentage of the overall volume
The drill-down listing SHALL present, for every listed item, both the
percentage of the immediate parent AND the percentage of the overall
volume, in distinct labeled columns at every level of the hierarchy. The
volume denominator SHALL be the OS-reported used bytes of the containing
volume (the same ground truth the storage Overview uses); the percentage
formatting SHALL come from the same storage-percentage helper as the
parent percentage.

#### Scenario: Both percentages are visible
- **WHEN** a user opens a drill-down listing at any hierarchy level
- **THEN** each item shows `% of Parent` and `% of Volume`, and
  `% of Volume` keeps the whole-volume denominator as the user drills

### Requirement: Treemap hover shows full canonical metadata
Hovering over a rendered treemap rectangle SHALL display the item's full
path, logical size, allocated (on-disk) size with its honest unknown/
partial qualifiers, percentage of the immediate parent, and percentage of
the volume. The rendered hover text and the accessible description SHALL
derive from the same shared metadata builder so they cannot drift.

#### Scenario: Hover metadata is complete
- **WHEN** a user hovers over a treemap rectangle
- **THEN** the hover shows the full path, size, size on disk, `% of
  parent`, and `% of volume`, updating as the pointer moves

### Requirement: Below-minimum items aggregate into an "Other small items" tile
Treemap tiles whose share of the current scope falls below the minimum
visible size SHALL NOT be rendered as overlapping or out-of-container
clamped rectangles. When at least two such items exist, they SHALL
aggregate into one deterministic synthetic tile named `Other small items`
that reports its combined logical size, member count, and allocated-size
qualifier; the aggregate SHALL be hoverable but SHALL NOT navigate or
activate any single member. When exactly one item is below the threshold it
SHALL remain an individual tile with honest (possibly sub-minimum)
geometry. All tiles SHALL tile their container exactly: no overlaps, full
containment.

#### Scenario: Many tiny siblings aggregate
- **WHEN** a treemap scope contains many items below the minimum visible
  share
- **THEN** they appear as one `Other small items` tile with combined
  metadata, and no rectangle overlaps another or escapes the viewport

#### Scenario: Aggregation is deterministic
- **WHEN** the same scope is laid out repeatedly
- **THEN** the catch-all membership, combined size, and layout are
  identical across runs

### Requirement: Clicking a file tile opens the browser at the containing folder
Clicking a treemap rectangle representing a file SHALL open or focus the
standard column-view file-browsing surface at the file's containing folder
with the file selected, without performing any file operation on it
(no open-with/launch). Directory rectangles keep drilling the treemap in
place.

#### Scenario: File tile activation is navigation only
- **WHEN** a user clicks a file rectangle
- **THEN** the storage panel closes, the column view shows the containing
  folder, the file is selected, and no file is opened or executed
