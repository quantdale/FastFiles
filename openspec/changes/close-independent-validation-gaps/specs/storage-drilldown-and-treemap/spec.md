## MODIFIED Requirements

### Requirement: Hierarchical Drill-Down with Percentage Context
The system SHALL present a hierarchical drill-down of a selected volume or folder scope in which every listed item shows its size, its percentage of its immediate parent, and its percentage of the overall volume, at every level of the hierarchy. The percentage-of-parent SHALL be the row's subtree size expressed as a percentage of the complete size of its immediate parent, where the complete parent size includes all of the parent's children (files and directories), and SHALL be "—" when the parent total is zero or not yet known, or for rows that have no normal parent (for example, the root of the current scope).

#### Scenario: Drilling into a subdirectory
- **WHEN** a user drills into a folder within the storage drill-down view
- **THEN** the system SHALL display that folder's children with each child's size, its percentage of the complete immediate parent (files and directories), and its percentage of the overall volume

#### Scenario: Parent containing both files and directories
- **WHEN** a non-root folder's parent contains other folders and files, and all their sizes are known
- **THEN** the percentage-of-parent for each child SHALL equal `childSubtreeSize / completeParentSize * 100` using the entire parent (files and directories), and the percentages of all children SHALL sum to 100% within the documented rounding tolerance

#### Scenario: Parent containing only files
- **WHEN** a non-root folder's parent contains only files
- **THEN** every file child SHALL display a percentage-of-parent equal to `fileSize / completeParentSize * 100`, and the percentages SHALL sum to 100% within the documented rounding tolerance

#### Scenario: Parent containing only directories
- **WHEN** a non-root folder's parent contains only directories
- **THEN** every directory child SHALL display a percentage-of-parent equal to `subtreeSize / completeParentSize * 100`, and the percentages SHALL sum to 100% within the documented rounding tolerance

#### Scenario: Zero-size parent
- **WHEN** a row's immediate parent has a total size of zero (or the parent total is `Pending`/`NotFound`)
- **THEN** every child of that parent SHALL display "—" for percentage-of-parent rather than a divide-by-zero value, and SHALL re-render the correct percentage in place once the parent total resolves

#### Scenario: Individual zero-size child
- **WHEN** a child within a non-zero-size parent has a size of zero
- **THEN** that child SHALL display "—" for percentage-of-parent (matching the convention that zero-size children are rendered as "—" for visual consistency with other zero-value rows), without a divide-by-zero error, and the other children SHALL retain their correct percentages

#### Scenario: Very large synthetic sizes and overflow safety
- **WHEN** a parent or child size exceeds values for which an unsafe `uint64` cross-multiplication would overflow (for example, multi-ten-terabyte sizes)
- **THEN** the percentage-of-parent display SHALL be computed with floating-point division without integer overflow, and sorting by percentage-of-parent SHALL produce a correct, deterministic order without overflow

#### Scenario: Percentage context recalculates at each level
- **WHEN** a user drills further into a child folder
- **THEN** percentage-of-parent SHALL recompute relative to the newly entered folder, while percentage-of-volume SHALL continue to reflect the total size of the whole volume

#### Scenario: Root of current scope
- **WHEN** a row is the root of the current drill-down scope (has no normal parent)
- **THEN** the row SHALL display "—" for percentage-of-parent and SHALL sort as if its percentage were zero, while its children remain parent-relative to it

### Requirement: Sorting Storage Views
Drill-down, largest-folders, and largest-files listings SHALL support sorting by size, name, type, last-modified date, and percentage-of-parent, in either ascending or descending order. Sorting by percentage-of-parent SHALL be deterministic and overflow-safe, and SHALL use the same parent-relative percentage values shown to the user.

#### Scenario: Sorting a listing by size
- **WHEN** a user selects size as the sort field for a drill-down, largest-folders, or largest-files listing
- **THEN** the system SHALL reorder the listing by size and SHALL allow toggling between ascending and descending order

#### Scenario: Sorting a listing by name, type, or date
- **WHEN** a user selects name, type, or last-modified date as the sort field
- **THEN** the system SHALL reorder the listing accordingly, with ascending/descending toggling available as with size

#### Scenario: Sorting by percentage-of-parent is deterministic
- **WHEN** a user sorts a listing by percentage-of-parent and two or more rows have equal or nearly-equal percentages
- **THEN** the system SHALL order those rows deterministically using a stable secondary key (for example, name), and the relative order for equal percentages SHALL not change between repeated sorts

#### Scenario: Sorting by percentage-of-parent is overflow-safe
- **WHEN** a listing contains rows whose sizes are large enough that `size * 1000000ULL` would overflow an unsigned 64-bit integer
- **THEN** sorting by percentage-of-parent SHALL still produce the correct order without integer overflow (for example, via exact cross-multiplication in extended precision or floating-point comparison)
