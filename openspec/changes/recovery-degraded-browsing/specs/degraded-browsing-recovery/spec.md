# Degraded-Browsing Recovery

Acceptance-style requirements for making the development build usable as a basic file manager through the existing degraded-mode path. No privileged-path, security-model, or wire-format-layout changes.

## ADDED Requirements

### Requirement: Requirement 1: Engine discovery

- 1.1 The UI SHALL resolve candidate engine executable paths in the order: UI executable's own directory, sibling `engine` directory of the UI executable's parent (normalized), `%ProgramFiles%\FastFiles`.
- 1.2 The UI SHALL launch only candidates that exist as regular files.
- 1.3 The UI SHALL NOT search the PATH or the disk for the engine executable.
- 1.4 The UI SHALL log the resolved engine path and any launch failure (with the OS error code) to the diagnostics log.

#### Scenario: Development build layout

- **Given** the UI executable is at `build/Debug/src/ui/FastFiles.exe`
- **When** the UI resolves engine candidates
- **Then** the first candidate is `build/Debug/src/ui/`, the second is `build/Debug/src/engine/`, and the third is `%ProgramFiles%\FastFiles\`
- **And** only existing regular files are launched

#### Scenario: Installed layout

- **Given** the UI executable is at `C:\Program Files\FastFiles\FastFiles.exe`
- **When** the UI resolves engine candidates
- **Then** the first candidate is `C:\Program Files\FastFiles\` (where the engine is co-located)
- **And** the second and third candidates are never reached

#### Scenario: Launch failure logging

- **Given** no valid engine executable exists in any candidate location
- **When** the UI attempts to launch the engine
- **Then** the UI logs each failed launch attempt with the OS error code to `diagnostics.log`
- **And** the UI enters the unavailable state after exhausting attempts
### Requirement: Requirement 2: Bounded connection lifecycle

- 2.1 The UI SHALL attempt to reach the engine at most 5 times per cycle at the existing cadence.
- 2.2 After the attempts are exhausted, the UI SHALL enter an explicit "engine unavailable" state and SHALL NOT silently retry forever.
- 2.3 The unavailable state SHALL offer a user-visible retry that restarts the cycle.
- 2.4 A lost connection SHALL be re-established through the same bounded cycle, with the connection state visible throughout.

#### Scenario: Engine startup retry exhaustion

- **Given** the engine process is not running
- **When** the UI attempts to connect 5 times with 2-second cadence
- **Then** the UI displays "Engine unavailable — click to retry" in the status chip
- **And** the UI stops attempting automatic reconnection
- **And** the user can click the chip to restart the connection cycle

#### Scenario: Lost connection recovery

- **Given** the UI is connected to the engine
- **When** the engine process terminates unexpectedly
- **Then** the UI attempts to reconnect up to 5 times
- **And** the status chip shows the connection state throughout (Starting ? Connected ? Starting ? … ? Unavailable)
- **And** if reconnection succeeds, the UI returns to the Connected state

### Requirement: Requirement 3: State visibility

- 3.1 The UI SHALL display distinct states: starting, connected with the index service active, connected in degraded mode, and engine unavailable.
- 3.2 The UI SHALL remain responsive in every state.
- 3.3 The UI SHALL NOT show settings or modal surfaces at launch without user action (the settings dialog previously covered the browser on every launch).

#### Scenario: Distinct state display

- **Given** the UI is in various connection states
- **When** the status chip is rendered
- **Then** starting state shows "Starting engine…"
- **And** connected with index shows "Instant search: enabled"
- **And** connected in degraded mode shows "Instant search: basic (degraded mode)"
- **And** unavailable state shows "Engine unavailable — click to retry"

#### Scenario: No modal at launch

- **Given** the user launches the UI from the development build
- **When** the application window appears
- **Then** the settings dialog is not shown automatically
- **And** the main browser view is visible and responsive
- **And** the user can open settings via the Settings menu or Ctrl+,

### Requirement: Requirement 4: Publish-failure surfacing

- 4.1 When a requested directory's snapshot publish fails, the engine SHALL notify the requesting client with `DirectoryErrorReason::PublishFailed`.
- 4.2 The UI SHALL render a clear error message for that column and SHALL NOT keep showing stale contents.

#### Scenario: Snapshot publish failure

- **Given** a directory's snapshot exceeds the 2×8 MiB slot capacity
- **When** the UI requests that directory
- **Then** the engine sends `PublishFailed` to the UI
- **And** the column displays a clear error message (not stale contents)
- **And** other columns continue to show their current data

### Requirement: Requirement 5: Watcher safety

- 5.1 The engine SHALL bound the number of concurrently watched roots (32) and evict the least-recently-watched root when the bound is reached.
- 5.2 On change-buffer overflow the engine SHALL log the event, re-enumerate the directory (republishing a fresh generation), and re-arm the watch; after 8 consecutive overflows it SHALL log and stop that watch.

#### Scenario: Watcher bound eviction

- **Given** 32 directories are being watched
- **When** the user browses a 33rd directory
- **Then** the least-recently-watched directory is removed from the watch set
- **And** the new directory is added to the watch set
- **And** a diagnostic log entry records the eviction

#### Scenario: Change-buffer overflow recovery

- **Given** a watched directory has a change-buffer overflow (`ERROR_NOTIFY_ENUM_DIR`)
- **When** the overflow is detected
- **Then** the engine logs the overflow event
- **And** re-enumerates the directory and republishes a fresh generation
- **And** re-arms the watch
- **And** if 8 consecutive overflows occur, the engine logs a final message and stops watching that directory

### Requirement: Requirement 6: Honest search scopes

- 6.1 Search scope labels SHALL describe the data actually searched (browsed locations), never implying whole-drive or all-indexed coverage.
- 6.2 Degraded mode SHALL keep browsed-snapshot scopes available while the index service is not active; scopes SHALL be labelled as browsed coverage and SHALL NOT imply whole-drive or all-indexed coverage. When persistent indexed coverage is active, indexed labels and persistent-only filtering SHALL identify that path.

#### Scenario: Accurate scope labels

- **Given** the user has browsed 3 folders across 2 drives without the index service
- **When** the search scope dropdown is displayed
- **Then** available scopes include "Current Folder", "Current Folder + Browsed Subfolders", "Current Drive — Browsed Folders Only", and "All Browsed Locations"
- **And** no scope implies whole-drive or all-indexed coverage
- **And** broad scopes remain backed by the browsed snapshot while the service is inactive
- **And** the UI reports the browsed-snapshot fallback rather than silently rewriting the selected scope

### Requirement: Requirement 7: Acceptance

- 7.1 Launching from the development build tree SHALL reach the engine and display drive contents.
- 7.2 Folder navigation (=5 levels), back/up navigation, file opening, and basic search over browsed folders SHALL work without the privileged service.
- 7.3 A missing or unreachable engine SHALL produce the unavailable state with a visible message and a working retry, never an infinite silent loop.

#### Scenario: Development build launches successfully

- **Given** the UI is launched from `build/Debug/src/ui/FastFiles.exe`
- **When** the application starts
- **Then** the engine is discovered at `build/Debug/src/engine/FastFilesEngine.exe`
- **And** the engine process starts successfully
- **And** the UI connects to the engine
- **And** drive contents are displayed in the column view

#### Scenario: Deep folder navigation without service

- **Given** the UI is running in degraded mode (no index service)
- **When** the user navigates through 5 or more levels of folders
- **Then** all folder contents are displayed correctly
- **And** back/up navigation works at every level
- **And** files can be opened
- **And** search returns results from browsed folders

#### Scenario: Missing engine produces visible unavailable state

- **Given** the engine executable is missing from all candidate locations
- **When** the UI is launched
- **Then** the UI attempts to connect 5 times
- **Then** the UI displays "Engine unavailable — click to retry" in the status chip
- **And** the UI does not silently retry forever
- **And** clicking the chip restarts the connection cycle
