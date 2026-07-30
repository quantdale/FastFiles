## ADDED Requirements

### Requirement: Breadcrumb Mode Display
The system SHALL display the current navigation context's path as a sequence of clickable segments (one per path component, e.g. `This PC > C: > Users > me > Documents`) whenever the address bar is not in editable-text mode. Clicking any segment SHALL navigate that same navigation context to the ancestor path represented by that segment.

#### Scenario: Breadcrumb reflects current location
- **WHEN** a navigation context's current path is `C:\Users\me\Documents`
- **THEN** the address bar SHALL render breadcrumb segments for `This PC`, `C:`, `Users`, `me`, and `Documents`, each individually clickable

#### Scenario: Clicking an ancestor segment navigates up
- **WHEN** the user clicks the `Users` segment while the current path is `C:\Users\me\Documents`
- **THEN** the navigation context SHALL navigate to `C:\Users` and the breadcrumb SHALL update to reflect the new current path

#### Scenario: Breadcrumb updates after any navigation
- **WHEN** the navigation context's current path changes for any reason (folder click, sidebar click, back/forward, committed path entry)
- **THEN** the address bar, if currently in breadcrumb mode, SHALL re-render to show the new current path

### Requirement: Editable-Text Mode Entry
The system SHALL provide an editable-text mode in which the address bar displays the current path as plain, fully selectable/editable text, entered independently of breadcrumb mode. The system SHALL support switching into editable-text mode by clicking the trailing empty space of the breadcrumb trail, by a dedicated toggle affordance, or by a keyboard shortcut, and SHALL support reverting to breadcrumb mode without navigating when the user presses Escape or moves focus away without committing.

#### Scenario: Switching to editable-text mode preserves current path as text
- **WHEN** the user activates editable-text mode while the current path is `C:\Users\me\Documents`
- **THEN** the address bar SHALL display an editable text field pre-filled with `C:\Users\me\Documents` with its contents selected

#### Scenario: Escaping editable-text mode discards typed input
- **WHEN** the user types a different path into the editable-text field and then presses Escape without pressing Enter
- **THEN** the address bar SHALL revert to breadcrumb mode showing the unchanged current path, and the navigation context SHALL NOT navigate anywhere

#### Scenario: Losing focus without committing reverts to breadcrumb mode
- **WHEN** the editable-text field has uncommitted typed text and the user clicks elsewhere in the application without pressing Enter
- **THEN** the address bar SHALL revert to breadcrumb mode showing the unchanged current path, and the navigation context SHALL NOT navigate anywhere

### Requirement: Direct Path Entry and Parsing
The system SHALL accept a typed or pasted path in editable-text mode and, upon commit (Enter), parse it through a fixed, ordered pipeline before attempting navigation: trim surrounding whitespace, strip a wrapping pair of quote characters if present, normalize one or more trailing path separators, normalize forward slashes to backslashes, expand `%ENVIRONMENT_VARIABLE%`-style tokens, and canonicalize any embedded relative segments (e.g. `..`) against the parsed absolute root. The system SHALL accept absolute drive-letter paths and UNC paths as valid roots, and SHALL reject paths lacking a recognized absolute root (i.e. plain relative paths with no drive letter or UNC prefix) as invalid input rather than resolving them against an implicit base.

#### Scenario: Pasted path with wrapping quotes and trailing separator is accepted
- **WHEN** the user pastes `"D:\Projects\FastFiles\"` into editable-text mode and presses Enter
- **THEN** the system SHALL parse it as `D:\Projects\FastFiles` and attempt navigation to that path

#### Scenario: Forward-slash path is normalized
- **WHEN** the user types `C:/Users/me/Documents` and presses Enter
- **THEN** the system SHALL normalize it to `C:\Users\me\Documents` before attempting navigation

#### Scenario: Environment-variable token is expanded
- **WHEN** the user types `%USERPROFILE%\Documents` and presses Enter
- **THEN** the system SHALL expand `%USERPROFILE%` to the current user's profile directory before attempting navigation

#### Scenario: UNC path is accepted as a valid absolute root
- **WHEN** the user types `\\server\share\folder` and presses Enter
- **THEN** the system SHALL treat `\\server\share\folder` as a valid absolute UNC path and attempt navigation to it

#### Scenario: Embedded relative segment is canonicalized against an absolute root
- **WHEN** the user types `C:\Users\me\..\you` and presses Enter
- **THEN** the system SHALL canonicalize the path to `C:\Users\you` and attempt navigation to that path

#### Scenario: Bare relative path is rejected, not silently resolved
- **WHEN** the user types `..\subfolder` (no drive letter or UNC prefix) and presses Enter
- **THEN** the system SHALL treat the input as syntactically invalid and SHALL NOT attempt to resolve it against the navigation context's current path or any other implicit base

### Requirement: Invalid and Inaccessible Path Feedback
The system SHALL distinguish, without crashing or hanging in any case, between three outcomes of a committed path entry: syntactically invalid input, well-formed input that does not resolve to an existing location, and well-formed input that resolves to an existing but inaccessible (permission-denied) location. For the first two cases, the system SHALL mark the address bar as invalid with distinguishing inline text and SHALL NOT navigate the context. For the third case, the system SHALL proceed with navigation and rely on the destination navigation surface's own permission-denied error state.

#### Scenario: Syntactically invalid path is rejected before navigation
- **WHEN** the user commits a path containing a reserved character outside any recognized escape (e.g. `C:\Users\me\bad<name>`)
- **THEN** the address bar SHALL indicate the input is invalid with explanatory text, keep focus in the field, and the navigation context SHALL NOT change its current path

#### Scenario: Well-formed but nonexistent path is rejected before navigation
- **WHEN** the user commits a well-formed absolute path that does not exist on disk
- **THEN** the address bar SHALL indicate the location no longer exists (or was never found) with explanatory text, and the navigation context SHALL NOT change its current path

#### Scenario: Well-formed, existing, but permission-denied path proceeds to navigation
- **WHEN** the user commits a well-formed absolute path that exists but the current user lacks permission to list its contents
- **THEN** the navigation context SHALL navigate to that path and the destination's navigation surface SHALL display its own permission-denied state, rather than the address bar blocking the navigation

### Requirement: Back and Forward Navigation History
The system SHALL maintain, for each navigation context independently, a linear back/forward history of folder-change navigation events. The system SHALL record a new history entry only when the context's current directory changes (folder descent, breadcrumb click, committed path entry, sidebar click, or an equivalent folder-change event) and SHALL NOT record a new entry for selection changes, scrolling, or other actions that do not change the context's current directory. Invoking Back or Forward SHALL move the context to the adjacent recorded directory without pushing a new history entry, and navigating to a new directory after using Back SHALL truncate any stale forward entries beyond the current position.

#### Scenario: Folder descent is recorded as a history entry
- **WHEN** a navigation context currently at `C:\Users\me` descends into `C:\Users\me\Documents` by folder selection
- **THEN** the context's history SHALL record `C:\Users\me\Documents` as reachable via Back to `C:\Users\me`

#### Scenario: Selection change alone does not create a history entry
- **WHEN** the user selects a different file within an already-displayed column without descending into a new folder
- **THEN** the navigation context's history SHALL remain unchanged

#### Scenario: Back navigation does not push a new entry
- **WHEN** the user invokes Back after having navigated from `C:\Users\me` to `C:\Users\me\Documents`
- **THEN** the context's current directory SHALL become `C:\Users\me` and no new history entry SHALL be pushed

#### Scenario: Navigating after Back truncates forward history
- **WHEN** the user invokes Back (now at `C:\Users\me`) and then navigates to `C:\Users\me\Downloads` via a new folder selection
- **THEN** the previously available Forward entry for `C:\Users\me\Documents` SHALL no longer be reachable via Forward

#### Scenario: History is independent per tab
- **WHEN** two open tabs have each independently navigated through different sequences of folders
- **THEN** invoking Back or Forward in one tab SHALL affect only that tab's current directory and history, leaving the other tab's history and current directory unchanged

### Requirement: Drive and Volume Selection
The system SHALL provide a means of selecting an enumerated local drive/volume and navigating the active navigation context to that drive's root path.

#### Scenario: Selecting a drive navigates to its root
- **WHEN** the user selects drive `D:` from the drive-selection control
- **THEN** the active navigation context SHALL navigate to `D:\`

#### Scenario: Selecting an unavailable or disconnected drive gives clear feedback
- **WHEN** the user selects a drive that is enumerated but currently unreadable (e.g. a removed removable drive)
- **THEN** the system SHALL attempt navigation and SHALL surface the resulting nonexistent/inaccessible-path feedback rather than crashing or hanging
