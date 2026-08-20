# multi-selection-and-dragdrop delta — harden-selection-model-semantics

## MODIFIED Requirements

### Requirement: Single-Click Selection
Clicking an item within an active pane, without a modifier key held, SHALL clear that pane's existing selection and select only the clicked item. The clicked item SHALL become the pane's current selection anchor and focused item for subsequent range-selection behavior.

#### Scenario: Plain click replaces selection
- **WHEN** a user has multiple items selected in a pane and clicks a different item without holding any modifier key
- **THEN** the previous selection SHALL be cleared, only the clicked item SHALL be selected, and that item SHALL become the anchor and focus

#### Scenario: Plain click establishes an anchor from empty state
- **WHEN** a pane has no current selection anchor and the user plain-clicks an item
- **THEN** that item SHALL become the sole selected item, selection anchor, and focused item

### Requirement: Ctrl-Click Toggle Selection
Ctrl-clicking an item within an active pane SHALL toggle that item's membership in the pane's current selection without affecting the selection state of any other item in that pane. Focus SHALL follow the clicked item. Ctrl-click SHALL NOT silently replace the entire selection.

#### Scenario: Ctrl-click adds an item to the selection
- **WHEN** a user has one or more items selected in a pane and Ctrl-clicks an unselected item
- **THEN** the clicked item SHALL be added to the selection while previously selected items remain selected, and focus SHALL move to the clicked item

#### Scenario: Ctrl-click removes an already-selected item
- **WHEN** a user Ctrl-clicks an item that is already part of the current selection
- **THEN** that item SHALL be removed from the selection while other selected items remain selected, and focus SHALL move to the clicked item

#### Scenario: Ctrl-click is the first selection interaction
- **WHEN** no selection anchor exists and the user Ctrl-clicks an item
- **THEN** the item SHALL be toggled into the selection and the pane SHALL establish a deterministic valid anchor/focus for subsequent keyboard or Shift-range interaction

### Requirement: Shift-Click Range Selection
Shift-clicking an item within an active pane SHALL select the contiguous visual range of items between the pane's current selection anchor and the clicked item. The range SHALL be recomputed from that same anchor on subsequent Shift-clicks; Shift-click SHALL update focus to the clicked endpoint without silently re-anchoring to the previous endpoint.

#### Scenario: Shift-click selects a contiguous range
- **WHEN** a user has selected a single item as the anchor and then Shift-clicks a different item further down or up the same pane
- **THEN** every item visually between the anchor and the clicked item, inclusive, SHALL become selected and focus SHALL move to the clicked endpoint

#### Scenario: A subsequent Shift-click reuses the original anchor
- **WHEN** a user Shift-clicks a new item after already performing one Shift-click range selection from the same anchor
- **THEN** the selected range SHALL be recomputed from the original anchor to the newly clicked item, replacing the previous range, and the original anchor SHALL remain the range origin

#### Scenario: Shift-click without an established anchor is deterministic
- **WHEN** no valid selection anchor exists and the user Shift-clicks an item
- **THEN** FastFiles SHALL establish a valid single-item selection/anchor/focus at the clicked item rather than constructing a range from an invalid sentinel

### Requirement: Select All Visible Items
Ctrl+A within an active pane SHALL select every item currently visible/listed in that pane, SHALL establish a valid deterministic focus/anchor when the pane is non-empty, and SHALL NOT affect the selection state of any other pane. On an empty pane it SHALL leave selection empty with no valid anchor/focus.

#### Scenario: Ctrl+A selects all items in the active pane
- **WHEN** a user presses Ctrl+A while a non-empty pane is active
- **THEN** every item currently listed in that pane SHALL become selected and the pane SHALL retain a valid anchor/focus

#### Scenario: Ctrl+A does not select items in other panes
- **WHEN** a user presses Ctrl+A while one pane is active and another pane is also visible with its own listed items and selection
- **THEN** only the active pane's items SHALL be selected, and the other pane's selection state SHALL remain unchanged

#### Scenario: Ctrl+A on an empty pane is a stable no-op
- **WHEN** a user presses Ctrl+A while the active pane contains no listed items
- **THEN** selection SHALL remain empty and anchor/focus SHALL remain invalid rather than referencing a nonexistent row

### Requirement: Selection Scoped to Active Pane
Each pane SHALL maintain its own independent selection state. Selecting items in one pane SHALL NOT clear or alter the selection state maintained by another pane, and file operations SHALL act on the selection of the pane that is currently active.

#### Scenario: Selecting in a child column does not clear the parent column's selection state
- **WHEN** a user selects a folder in a parent column, which populates a child column, and then selects items within that child column
- **THEN** the parent column's own selection highlight SHALL remain intact and unaffected by the child column's selection

#### Scenario: Modifier selection in one dual pane leaves the other pane untouched
- **WHEN** both panes contain selections and the user performs plain/Ctrl/Shift/Ctrl+A selection changes in the active pane
- **THEN** the inactive pane SHALL retain exactly its prior selected membership, anchor, and focus state
