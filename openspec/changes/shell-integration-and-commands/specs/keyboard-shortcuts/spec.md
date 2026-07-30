## ADDED Requirements

### Requirement: Default Shortcut Set for Core Actions
`FastFiles` SHALL ship a default keyboard shortcut for each of the following core actions: navigate back, navigate forward, refresh, copy, cut, paste, delete, rename, select all, open/focus search, and open/focus the path (address) entry, in addition to opening the command palette.

#### Scenario: Default bindings are active on first run
- **WHEN** `FastFiles` runs with no persisted shortcut customization present
- **THEN** each of Back (Alt+Left), Forward (Alt+Right), Refresh (F5), Copy (Ctrl+C), Cut (Ctrl+X), Paste (Ctrl+V), Delete (Delete), Rename (F2), Select All (Ctrl+A), Focus Search (Ctrl+F), Focus Path Entry (Ctrl+L), and Open Command Palette (Ctrl+Shift+P) SHALL invoke its corresponding command via the shared command registry

#### Scenario: Permanent delete uses a distinct default binding
- **WHEN** a user presses Shift+Delete on a selection
- **THEN** the permanent-delete action exposed by `file-operations-core` SHALL be invoked, distinct from the Recycle-Bin default-delete binding on the plain Delete key

### Requirement: Shortcut Scope Model
Each shortcut binding SHALL be classified as either Global (dispatched ahead of per-view input handling and active no matter which control, pane, column, or tab currently owns keyboard focus within the application) or Active-View (active only in whichever pane or tab is currently the active one, regardless of which control within that view has focus).

#### Scenario: An Active-View-scoped shortcut respects the active pane
- **WHEN** dual-pane mode is active and a user presses the Back shortcut
- **THEN** navigation history SHALL move back in whichever pane is currently the active pane, not in the inactive pane

#### Scenario: A Global-scoped shortcut ignores which view is active
- **WHEN** any view, pane, or dialog within `FastFiles` currently holds keyboard focus
- **THEN** a Global-scoped shortcut SHALL still be recognized and dispatched

### Requirement: Global Search Hotkey Works Regardless of Focus Context
The keyboard shortcut for opening or focusing search SHALL be registered as a Global-scoped binding, checked at the top of the application's input-handling chain, so that it activates search without requiring the user to first navigate to any specific folder or view.

#### Scenario: Search hotkey works from a deeply nested column
- **WHEN** a user has navigated several levels deep in Column View and the rightmost column currently has keyboard focus
- **THEN** pressing the search shortcut SHALL activate search immediately, with no prior navigation required

#### Scenario: Search hotkey works from a secondary pane
- **WHEN** dual-pane mode is active and the secondary pane currently has keyboard focus
- **THEN** pressing the search shortcut SHALL activate search immediately, identically to when the primary pane has focus

### Requirement: Persisted Shortcut Customization Data Model
`FastFiles` SHALL allow keyboard shortcuts to be rebound to a different key combination, persisting only the differences from the built-in default map, keyed by each command's stable identifier, so that the customization is available to be read and edited by the future settings interface.

#### Scenario: A rebound shortcut persists across restarts
- **WHEN** a user rebinds a command to a new key combination and restarts `FastFiles`
- **THEN** the rebound key combination SHALL be in effect, read from the persisted customization data

#### Scenario: A new default shortcut is not shadowed by an old customization file
- **WHEN** a later version of `FastFiles` introduces a default shortcut for a command that has no entry in the user's persisted customization file
- **THEN** that command's built-in default SHALL apply, unaffected by the presence of unrelated customizations in the file

#### Scenario: An unresolvable persisted binding is dropped without crashing
- **WHEN** the persisted customization file contains a binding for a command identifier that no longer exists
- **THEN** `FastFiles` SHALL ignore and log that entry at load time rather than failing to start or crashing

### Requirement: Conflict Detection on Rebinding
`FastFiles` SHALL detect when a user attempts to bind a key combination that is already bound to a different command within an overlapping scope, and SHALL require the conflict to be explicitly resolved before the new binding is accepted.

#### Scenario: Rebinding to an already-bound key combination
- **WHEN** a user attempts to bind a command to a key combination already bound to a different command in the same or an overlapping scope
- **THEN** `FastFiles` SHALL flag the conflict and SHALL NOT silently apply the new binding

#### Scenario: User resolves the conflict by reassigning
- **WHEN** a user is shown a rebinding conflict and chooses to proceed
- **THEN** the previously conflicting command's binding SHALL be removed and the key combination SHALL be bound to the newly chosen command instead

#### Scenario: User cancels out of a conflicting rebind
- **WHEN** a user is shown a rebinding conflict and chooses not to proceed
- **THEN** both commands SHALL retain their prior bindings, unchanged

### Requirement: Warning on Binding a Windows-Reserved Key Combination
`FastFiles` SHALL warn the user when attempting to bind a key combination that is reserved by Windows itself (for example, Alt+F4), noting that it may not be reliably received, while still allowing the binding to be saved for use within `FastFiles`' own input handling.

#### Scenario: Binding a system-reserved combination
- **WHEN** a user attempts to bind a command to a key combination reserved by Windows
- **THEN** `FastFiles` SHALL display a warning that the combination may not always be received, and SHALL still allow the user to save the binding if they choose to proceed
