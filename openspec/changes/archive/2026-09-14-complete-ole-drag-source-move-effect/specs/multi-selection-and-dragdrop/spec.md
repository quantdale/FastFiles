## ADDED Requirements

### Requirement: Drag-Out Source Completes MOVE Using Performed-Drop-Effect
When FastFiles is the OLE drag source, after `DoDragDrop` returns FastFiles SHALL inspect the negotiated effect and the standard `CFSTR_PERFORMEDDROPEFFECT` protocol. If the target reports MOVE and has not itself removed the source items, FastFiles SHALL delete those sources through the existing `file-operations-core` Recycle-Bin delete path. If the target already moved the files (typical Explorer HDROP MOVE), FastFiles SHALL NOT delete them a second time. Copy and Link effects SHALL NOT delete sources.

#### Scenario: Target copies and reports MOVE
- **WHEN** a user drags a FastFiles selection to a drop target that copies the HDROP paths and returns `DROPEFFECT_MOVE` without removing the sources
- **THEN** FastFiles SHALL remove the source items via the in-app delete path so the net effect is a move, not a duplicate

#### Scenario: Target already moved the files
- **WHEN** a user drags a FastFiles selection to a drop target that itself moves the HDROP paths (sources no longer exist) and returns `DROPEFFECT_MOVE`
- **THEN** FastFiles SHALL NOT attempt a second delete of those paths and SHALL NOT report a user-visible failure for the missing sources

#### Scenario: Copy drag-out leaves sources
- **WHEN** a user drags a FastFiles selection and the negotiated effect is `DROPEFFECT_COPY`
- **THEN** the source items SHALL remain in place

### Requirement: Link Drops Reserve Unique Destination Names Inside The Batch
When FastFiles creates shortcuts for a drop or link operation, two sources that share a basename SHALL receive distinct `.lnk` destination names in the target folder (the same intra-batch reservation `file-operations-core` uses for copy/move Keep Both), rather than the later shortcut overwriting the earlier one.

#### Scenario: Two same-basename sources linked into one folder
- **WHEN** a user creates links for `D:\a\report.txt` and `E:\b\report.txt` in the same destination folder
- **THEN** FastFiles SHALL produce two shortcut files with distinct names and SHALL NOT overwrite the first `.lnk` with the second

## MODIFIED Requirements

### Requirement: Native OLE Drag Source
`FastFiles` SHALL support dragging the current pane's selected items out of a FastFiles window using a native `IDropSource`/`IDataObject` implementation that exposes the selected items as `CF_HDROP`, so that dropping them onto a real Windows Explorer window or another Windows application succeeds using standard Windows drag-and-drop conventions. FastFiles SHALL complete a MOVE reported by the target as specified in "Drag-Out Source Completes MOVE Using Performed-Drop-Effect" rather than ignoring the `DoDragDrop` result.

#### Scenario: Dragging selected items into a real Explorer window
- **WHEN** a user drags a selection of items from a FastFiles pane and drops them onto a folder open in a real Windows Explorer window
- **THEN** the dropped items SHALL be copied or moved into that Explorer folder according to standard Windows drag-and-drop effect conventions, and a MOVE SHALL NOT leave the original items in place unless the user cancelled

#### Scenario: BeginFileDrag result is not discarded
- **WHEN** FastFiles initiates an OLE drag of the current selection
- **THEN** the negotiated drop effect returned by `DoDragDrop` SHALL be consumed by the source to decide whether sources must be removed
