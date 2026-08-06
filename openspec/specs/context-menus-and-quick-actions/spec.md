# context-menus-and-quick-actions Specification

## Purpose
Native Win32 context menus built from the shared command registry, composed per selection kind, with Open/Open With, copy-path actions, terminal-here, and delegation of file manipulation to existing capabilities.

## Requirements
### Requirement: Context Menu Built From the Shared Command Registry
`FastFiles` SHALL build every context menu by filtering the shared command registry (the same registry read by `command-palette` and `keyboard-shortcuts`) rather than maintaining a separate, menu-specific definition of any action, so that an action such as "Copy Path" is defined exactly once and stays consistent across menu, palette, and shortcut.

#### Scenario: A command's label and behavior are consistent across surfaces
- **WHEN** a command is invoked from the context menu
- **THEN** it SHALL execute the identical handler that the command palette and any bound keyboard shortcut for that same command would invoke, with no menu-specific reimplementation of the action's behavior

### Requirement: Selection-Appropriate Context Menu Composition
`FastFiles` SHALL present a right-click (or keyboard-invoked) context menu whose set of enabled actions depends on what is currently selected: a single file, a single folder, a multi-selection of same-kind items, a mixed multi-selection of files and folders, or no selection (empty area within the current view).

#### Scenario: Single file selected
- **WHEN** a user right-clicks a single selected file
- **THEN** the context menu SHALL include Open, Open With, Copy, Cut, Rename, Delete, Copy Path, Copy Relative Path, Open Containing Folder, and Properties, and SHALL NOT include Open Terminal/PowerShell Here

#### Scenario: Single folder selected
- **WHEN** a user right-clicks a single selected folder
- **THEN** the context menu SHALL include Open, Copy, Cut, Rename, Delete, Copy Path, Copy Relative Path, Open Terminal/PowerShell Here, and Properties

#### Scenario: Multi-selection of same-kind items
- **WHEN** a user right-clicks while multiple files (or multiple folders) are selected
- **THEN** the context menu SHALL include only the actions valid for every selected item (e.g. Copy, Cut, Delete, Copy Path, Properties), and SHALL NOT include single-item-only actions such as Rename or Open With

#### Scenario: Mixed multi-selection of files and folders
- **WHEN** a user right-clicks while both files and folders are selected together
- **THEN** the context menu SHALL include only the actions applicable to both kinds (e.g. Copy, Cut, Delete, Copy Path, Properties), and SHALL exclude actions applicable to only one kind (Open With, Open Terminal/PowerShell Here, Rename)

#### Scenario: Right-click on empty area with no selection
- **WHEN** a user right-clicks empty space within the current view with nothing selected
- **THEN** the context menu SHALL present actions appropriate to the containing folder itself (at minimum Paste, if the clipboard holds a compatible payload, and Open Terminal/PowerShell Here) rather than showing no menu or an item-oriented menu

### Requirement: Native Win32 Popup Menu Rendering
Context menus SHALL be rendered as native Win32 popup menus constructed with `CreatePopupMenu` and displayed with `TrackPopupMenuEx`, not as a custom-drawn control or an embedded managed-framework menu control.

#### Scenario: Mouse-invoked menu anchors at the cursor
- **WHEN** a user right-clicks an item or empty area with the mouse
- **THEN** the native popup menu SHALL be anchored at the current cursor position via `TrackPopupMenuEx`

#### Scenario: Keyboard-invoked menu anchors at the selection
- **WHEN** a user presses the context-menu key (or Shift+F10) while an item is selected
- **THEN** the native popup menu SHALL open anchored at the selected item's on-screen position, fully operable via arrow keys and Enter without requiring the mouse

### Requirement: Open and Open With Actions
`FastFiles` SHALL provide an Open action that, for a selected file, launches the file with its OS-associated default application via the Win32 shell (without FastFiles maintaining its own file-type-association logic), and, for a selected folder, delegates to the existing `column-view-browsing` navigation (selecting/populating that folder) rather than implementing new navigation behavior. `FastFiles` SHALL provide an Open With action for files that invokes the Windows "choose an application" picker.

#### Scenario: Opening a file invokes its default application
- **WHEN** a user selects Open on a single file
- **THEN** the file SHALL be launched via its OS-registered default application, and `FastFiles` SHALL NOT itself resolve or store file-type-to-application associations

#### Scenario: Opening a folder navigates rather than launches a process
- **WHEN** a user selects Open on a single folder
- **THEN** the existing Column View navigation SHALL populate the next column with that folder's contents, with no new process launched

#### Scenario: Open With shows the system application chooser
- **WHEN** a user selects Open With on a single file
- **THEN** the Windows "choose an application" picker SHALL be shown for that file, and the file SHALL be launched with the application the user selects there, if any

### Requirement: File-Manipulation Menu Entries Delegate to Existing Capabilities
Copy, Cut, Rename, and Delete menu entries SHALL invoke the existing `file-operations-core` (and, where relevant, `conflict-resolution`) implementation of those actions rather than re-implementing file-manipulation or conflict-handling logic within the context-menu surface.

#### Scenario: Delete from the context menu uses the existing delete flow
- **WHEN** a user selects Delete from the context menu
- **THEN** the same Recycle-Bin-by-default delete behavior implemented by `file-operations-core` SHALL execute, with no separate delete implementation defined by the context menu itself

#### Scenario: Paste after Copy triggers existing conflict resolution
- **WHEN** a user copies an item via the context menu and pastes it into a destination that already contains an item of the same name
- **THEN** the existing `conflict-resolution` replace/skip/rename/apply-to-all-remaining flow SHALL be shown, not a separate context-menu-defined conflict dialog

### Requirement: Copy Path Action
`FastFiles` SHALL provide a Copy Path action that places the full absolute file-system path of the current selection onto the clipboard as text.

#### Scenario: Copying the path of a single selected item
- **WHEN** a user selects Copy Path with exactly one item selected
- **THEN** that item's full absolute path SHALL be placed on the clipboard as plain text

#### Scenario: Copying paths of a multi-selection
- **WHEN** a user selects Copy Path with multiple items selected
- **THEN** each selected item's full absolute path SHALL be placed on the clipboard as plain text, one path per line, in the order the items appear in the current view

### Requirement: Copy Relative Path Action
`FastFiles` SHALL provide a Copy Relative Path action that places the selection's path, expressed relative to a resolved base folder, onto the clipboard as text. The base folder SHALL be the other pane's current location when dual-pane mode is active, and otherwise the current view's root (leftmost) column location.

#### Scenario: Relative path within the same volume as the base
- **WHEN** a user selects Copy Relative Path and the selection resides on the same volume as the resolved base folder
- **THEN** the clipboard SHALL contain the path expressed relative to that base folder

#### Scenario: No valid relative path exists
- **WHEN** a user selects Copy Relative Path and the selection resides on a different volume than the resolved base folder
- **THEN** `FastFiles` SHALL fall back to copying the item's full absolute path and SHALL indicate to the user that a relative path was not possible

### Requirement: Open Containing Folder Action
`FastFiles` SHALL provide an Open Containing Folder action that navigates the current view to the selected item's parent folder, with the item itself selected, by delegating to the existing Column View navigation mechanism.

#### Scenario: Invoked on a selected file
- **WHEN** a user selects Open Containing Folder on a file
- **THEN** the view SHALL navigate to and display that file's parent folder with the file selected, using the existing navigation entry point rather than new navigation logic

### Requirement: Open Terminal/PowerShell Here Action
`FastFiles` SHALL provide an Open Terminal/PowerShell Here action, available for a selected folder or for empty space within the current view, that launches a configured shell process (default PowerShell, falling back to the Windows Command Processor if unavailable) via `CreateProcess`, with the process's current directory set to the target folder. This action is a convenience integration and is explicitly not a replacement for a full terminal application.

#### Scenario: Invoked on a selected folder
- **WHEN** a user selects Open Terminal/PowerShell Here on a folder
- **THEN** a shell process SHALL be launched with its working directory set to that folder's path, passed only via the process's current-directory parameter and never interpolated into an executed command-line string

#### Scenario: Invoked with no selection
- **WHEN** a user selects Open Terminal/PowerShell Here on empty space within the current view
- **THEN** a shell process SHALL be launched with its working directory set to the folder currently backing that view

#### Scenario: Configured shell is unavailable
- **WHEN** the configured default shell executable cannot be located
- **THEN** `FastFiles` SHALL fall back to launching the Windows Command Processor, and SHALL show a clear, non-crashing error if no usable shell can be launched at all

### Requirement: Properties Action Delegates to In-App Properties View
`FastFiles` SHALL provide a Properties action that displays the selection's details using the existing in-app `properties-and-details` view, rather than launching the separate native Windows Properties dialog.

#### Scenario: Properties invoked on a single item
- **WHEN** a user selects Properties on a single file or folder
- **THEN** the in-app `properties-and-details` view SHALL be shown for that item

#### Scenario: Properties invoked on a multi-selection
- **WHEN** a user selects Properties with multiple items selected
- **THEN** the in-app `properties-and-details` view SHALL show the aggregate details applicable to the selection (such as combined size and item count) rather than failing or showing only the first item
