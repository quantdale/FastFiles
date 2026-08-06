# search-ui Specification

## Purpose
The search experience: debounced search-as-you-type, cancellation of stale in-flight searches, a sortable virtualized result list, scope selection that reflects index mode, search-to-navigation, and local search history.

## Requirements
### Requirement: Search-as-You-Type with Debounced Execution
`FastFiles` SHALL execute a search automatically as the user types into the search box, after a short, fixed delay measured from the most recent keystroke, so that rapid successive keystrokes do not each trigger a separate full query execution.

#### Scenario: A pause after typing triggers a search
- **WHEN** a user types characters into the search box and then pauses briefly
- **THEN** the UI SHALL dispatch a search for the current query text after that pause, without requiring the user to press Enter or click a search button

#### Scenario: Rapid typing does not dispatch a search per keystroke
- **WHEN** a user types several characters in quick succession, faster than the debounce delay
- **THEN** the UI SHALL NOT dispatch a separate search execution for each intermediate keystroke

### Requirement: Cancellation of Stale In-Flight Searches
When a new keystroke arrives while a previously dispatched search is still executing, the UI SHALL discard that search's results if and when they arrive, rather than displaying results for a query the user has already changed.

#### Scenario: Results for a superseded query are not shown
- **WHEN** a search dispatched for an earlier query text is still running and the user has since typed additional characters, changing the query
- **THEN** if the earlier search's results arrive after the newer input, the UI SHALL discard them and SHALL display results only for the current query text

### Requirement: Result List with Core Metadata Columns
The result list SHALL display, for each result, the filename, item type (file or folder), containing folder/location path, size, and last-modified time, and SHALL display a clear, distinct indication when a search completes with zero matches.

#### Scenario: Result row shows required metadata
- **WHEN** a search returns at least one matching entry
- **THEN** each row in the result list SHALL show that entry's filename, type, location path, size, and last-modified time

#### Scenario: No matches found is clearly indicated
- **WHEN** a search completes and no entries match the current query and scope
- **THEN** the UI SHALL display an explicit "no results" indication, distinguishable from the state where a search is still in progress

### Requirement: Sortable Result List
The result list SHALL be sortable by relevance, filename, path, size, modified date, and created date, using the underlying orders provided by the query engine, and SHALL visibly indicate which sort field and direction is currently active.

#### Scenario: Changing the sort field reorders results
- **WHEN** a user selects a sort field other than relevance (for example, size)
- **THEN** the result list SHALL reorder according to that field and SHALL visibly indicate the newly active sort field and direction

#### Scenario: Default sort is relevance
- **WHEN** a user performs a search without having changed the sort field
- **THEN** the result list SHALL be ordered by relevance by default

### Requirement: Search Scope Selector with Clear Indication
The UI SHALL provide a scope selector offering Current Folder, Current Folder and Subfolders, Current Drive, and All Indexed Locations, and SHALL visibly indicate the currently active scope at all times the search UI is in use.

#### Scenario: Active scope is always visible
- **WHEN** the search box has focus or results are displayed
- **THEN** the currently active search scope SHALL be visibly indicated, without requiring the user to open a menu to discover it

#### Scenario: Changing scope re-executes the current query
- **WHEN** a user changes the selected scope while a query is present in the search box
- **THEN** the UI SHALL re-execute the current query against the newly selected scope without requiring the user to retype it

### Requirement: Scope Availability Reflects Index Mode
When the privileged index path is unavailable (degraded mode), the scope selector SHALL disable scopes that would otherwise imply whole-volume or all-volume coverage, with a visible explanation, and SHALL re-enable them automatically if the privileged path becomes available during the session.

#### Scenario: Degraded mode narrows available scopes
- **WHEN** `FastFilesEngine` is operating in degraded mode (privileged service absent, declined, or disconnected)
- **THEN** the Current Drive and All Indexed Locations scope options SHALL be shown as disabled with a visible explanation, and the effective default scope SHALL be Current Folder and Subfolders

#### Scenario: Recovery of the privileged path re-enables broader scopes
- **WHEN** the privileged connection transitions to its active state during a session that was previously in degraded mode
- **THEN** the previously disabled scope options SHALL become selectable without requiring the user to restart the application

#### Scenario: Forced scope fallback is visibly announced
- **WHEN** a user's explicitly selected scope becomes unavailable because the privileged connection drops mid-session
- **THEN** the UI SHALL fall back to the next-narrower available scope and SHALL show a one-time inline notice explaining the change, rather than silently searching a narrower scope than what is displayed as selected

### Requirement: Search-to-Navigation Full Hierarchy Reconstruction
Selecting a search result SHALL populate Column View with every column from the volume root down to the matched entry, using the query engine's path reconstruction, and SHALL select the matched entry in its containing column.

#### Scenario: Selecting a deeply nested file populates the full column chain
- **WHEN** a user selects a search result located several directory levels below a volume root
- **THEN** Column View SHALL display a populated column for every intermediate directory from the volume root down to the matched entry's containing folder, with the matched entry selected in that final column

#### Scenario: Selecting a folder result populates through that folder
- **WHEN** a user selects a search result that is itself a folder
- **THEN** Column View SHALL populate columns from the volume root down to and including that folder, with the folder selected

#### Scenario: A broken path during navigation shows the existing in-column error state
- **WHEN** a user selects a search result whose path can no longer be fully resolved because part of it was renamed, moved, or deleted after the match was produced
- **THEN** Column View SHALL populate columns as far as still resolvable and SHALL display its existing in-column "no longer available" error state for the unresolved segment, rather than failing silently or crashing

### Requirement: Virtualized Rendering of Large Result Sets
The result list SHALL render only the rows currently within (or immediately adjacent to) the visible viewport, and SHALL NOT create a persistent UI element for every match when the result set contains far more entries than are visible at once.

#### Scenario: Scrolling a large result set only realizes visible rows
- **WHEN** a search returns several thousand matching entries
- **THEN** the UI SHALL only construct or update rendering state for the rows currently visible (plus a small scroll buffer), and SHALL NOT allocate a UI element for every one of the several thousand entries

#### Scenario: Jumping to the end of a large result set remains responsive
- **WHEN** a user scrolls or jumps directly to the end of a several-thousand-entry result set
- **THEN** the UI SHALL render the corresponding rows without a perceptible delay proportional to the total number of results

### Requirement: Local Configurable Search History
`FastFiles` SHALL locally record a history of previously executed searches, SHALL make that history available for recall in the search UI, and SHALL allow the user to clear the history or disable its recording entirely.

#### Scenario: Previous searches are available for recall
- **WHEN** a user has previously executed one or more searches and opens the search box again
- **THEN** the UI SHALL offer those previous queries for quick recall, stored only locally on the current machine

#### Scenario: User clears search history
- **WHEN** a user chooses to clear search history
- **THEN** all previously recorded queries SHALL be removed and SHALL no longer be offered for recall

#### Scenario: User disables search history recording
- **WHEN** a user disables search history recording
- **THEN** subsequently executed searches SHALL NOT be added to the history, while previously recorded entries (until separately cleared) remain unaffected
