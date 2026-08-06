# multi-selection-and-dragdrop Specification

## Purpose
The selection model (click, Ctrl/Shift ranges, select-all, per-pane scope) and native OLE drag source / drop target integrated with the standard file-operation path.

## Requirements
### Requirement: Single-Click Selection
Clicking an item within an active pane, without a modifier key held, SHALL clear that pane's existing selection and select only the clicked item.

#### Scenario: Plain click replaces selection
- **WHEN** a user has multiple items selected in a pane and clicks a different item without holding any modifier key
- **THEN** the previous selection SHALL be cleared and only the clicked item SHALL be selected

### Requirement: Ctrl-Click Toggle Selection
Ctrl-clicking an item within an active pane SHALL toggle that item's membership in the pane's current selection without affecting the selection state of any other item in that pane.

#### Scenario: Ctrl-click adds an item to the selection
- **WHEN** a user has one or more items selected in a pane and Ctrl-clicks an unselected item
- **THEN** the clicked item SHALL be added to the selection while previously selected items remain selected

#### Scenario: Ctrl-click removes an already-selected item
- **WHEN** a user Ctrl-clicks an item that is already part of the current selection
- **THEN** that item SHALL be removed from the selection while other selected items remain selected

### Requirement: Shift-Click Range Selection
Shift-clicking an item within an active pane SHALL select the contiguous visual range of items between the pane's current selection anchor and the clicked item.

#### Scenario: Shift-click selects a contiguous range
- **WHEN** a user has selected a single item as the anchor and then Shift-clicks a different item further down the same pane
- **THEN** every item visually between the anchor and the clicked item, inclusive, SHALL become selected

#### Scenario: A subsequent Shift-click re-anchors the range
- **WHEN** a user Shift-clicks a new item after already performing one Shift-click range selection from the same anchor
- **THEN** the selected range SHALL be recomputed from the same anchor to the newly clicked item, replacing the previous range

### Requirement: Select All Visible Items
Ctrl+A within an active pane SHALL select every item currently visible/listed in that pane, and SHALL NOT affect the selection state of any other pane.

#### Scenario: Ctrl+A selects all items in the active pane
- **WHEN** a user presses Ctrl+A while a pane is active
- **THEN** every item currently listed in that pane SHALL become selected

#### Scenario: Ctrl+A does not select items in other panes
- **WHEN** a user presses Ctrl+A while one pane is active and another pane is also visible with its own listed items
- **THEN** only the active pane's items SHALL be selected, and the other pane's selection state SHALL remain unchanged

### Requirement: Selection Scoped to Active Pane
Each pane SHALL maintain its own independent selection state. Selecting items in one pane SHALL NOT clear or alter the selection state maintained by another pane, and file operations SHALL act on the selection of the pane that is currently active.

#### Scenario: Selecting in a child column does not clear the parent column's selection state
- **WHEN** a user selects a folder in a parent column, which populates a child column, and then selects items within that child column
- **THEN** the parent column's own selection highlight SHALL remain intact and unaffected by the child column's selection

### Requirement: Native OLE Drag Source
`FastFiles` SHALL support dragging the current pane's selected items out of a FastFiles window using a native `IDropSource`/`IDataObject` implementation that exposes the selected items as `CF_HDROP`, so that dropping them onto a real Windows Explorer window or another Windows application succeeds using standard Windows drag-and-drop conventions.

#### Scenario: Dragging selected items into a real Explorer window
- **WHEN** a user drags a selection of items from a FastFiles pane and drops them onto a folder open in a real Windows Explorer window
- **THEN** the dropped items SHALL be copied or moved into that Explorer folder according to standard Windows drag-and-drop effect conventions

### Requirement: Native OLE Drop Target
`FastFiles` panes SHALL register as native OLE drop targets (`IDropTarget`) accepting `CF_HDROP` data, so that dragging items from a real Windows Explorer window, another FastFiles window, or another Windows application into a FastFiles pane succeeds.

#### Scenario: Dragging items from Explorer into FastFiles
- **WHEN** a user drags a selection of files from a real Windows Explorer window and drops them onto an active FastFiles pane
- **THEN** FastFiles SHALL accept the drop and transfer the dropped items into the target pane's current location

### Requirement: Drag-and-Drop Resolves to a Tracked File Operation
A completed drag-and-drop transfer SHALL be executed as a normal copy or move operation through the same worker-thread `IFileOperation` path used by keyboard- and menu-initiated operations, including conflict resolution and reversible-operation history where applicable.

#### Scenario: A dropped item that conflicts with an existing destination name
- **WHEN** a user drags an item into a FastFiles pane and its name collides with an existing item at the destination
- **THEN** FastFiles SHALL present the same in-app conflict-resolution dialog used for menu- and keyboard-initiated operations

#### Scenario: A completed drag-and-drop move is undoable
- **WHEN** a user drags an item to a new location within the same volume such that it is executed as a move, and the move completes successfully
- **THEN** that move SHALL be added to the reversible operation history exactly as a keyboard- or menu-initiated move would be

### Requirement: Drag Effect Determined by Modifier Keys
`FastFiles` SHALL determine whether a drag-and-drop operation performs a copy, move, or link based on standard Windows modifier-key conventions: Ctrl forces copy, Shift forces move, Ctrl+Shift forces a link, and no modifier defaults to move within the same volume or copy across volumes.

#### Scenario: Dragging without a modifier across volumes
- **WHEN** a user drags a selection from a pane showing one volume and drops it onto a location on a different volume without holding any modifier key
- **THEN** the operation SHALL be performed as a copy

#### Scenario: Dragging without a modifier within the same volume
- **WHEN** a user drags a selection and drops it onto a different folder on the same volume without holding any modifier key
- **THEN** the operation SHALL be performed as a move

#### Scenario: Holding Ctrl forces a copy
- **WHEN** a user holds Ctrl while dragging and dropping a selection within the same volume
- **THEN** the operation SHALL be performed as a copy rather than a move
