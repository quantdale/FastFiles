## Purpose

Make the primary Find -> reveal -> navigate journey preserve hierarchy, selection and location context from every entry point.

## ADDED Requirements

### Requirement: Typed Home session restoration reaches presentation

When the persisted location resolves to Home, startup SHALL apply Home to the navigation model, real active pane and chrome without filesystem enumeration of Home's empty path.

#### Scenario: Close and reopen Home
- **WHEN** a user closes on Home and reopens with session restoration enabled
- **THEN** the real Home pane and its actions are visible without a navigation command

### Requirement: Search reveal synchronizes the native workspace

Activating a search result SHALL leave Home/search mode in the active pane, establish the containing hierarchy and selected file or destination folder, expose the focused column, synchronize chrome and preserve the other pane.

#### Scenario: File result from Home
- **WHEN** search is opened from Home and a file result is activated
- **THEN** the actual containing folder and selected file are visible in Column View and the navigation model agrees

#### Scenario: Deep result
- **WHEN** a result lies beyond the initial horizontal viewport
- **THEN** its focused destination column is scrolled into view

### Requirement: Result context does not collide at DPI

Filename, location and secondary metadata SHALL be clipped or ellipsized within non-overlapping result-row bounds at supported DPI and narrow widths. Physical row geometry SHALL be scaled only once.

#### Scenario: Narrow high-DPI row
- **WHEN** a long location and metadata occupy a narrow result row at 120 or 192 DPI
- **THEN** location and metadata remain in separate bounds without overlapping

### Requirement: Bounded status feedback remains legible
Engine badges and workspace footer text SHALL remain on one line within their actual bounds and indicate omitted text with an ellipsis. Compact labels SHALL preserve degraded/unavailable/retry meaning and SHALL NOT claim privileged indexing outside the connected privileged state. Transient recovery/completion notices SHALL precede long path metadata. Full status descriptions and filesystem identities SHALL remain unchanged. Multi-line details and recovery text SHALL retain their wrapping behavior.

#### Scenario: Narrow degraded workspace
- **WHEN** the available workspace becomes narrow while degraded browsing is active
- **THEN** the badge visibly identifies basic/degraded search and the footer truncates long paths without painting a clipped second line

#### Scenario: Stale privileged flag during disconnection
- **WHEN** the engine is starting or unavailable while a prior privileged flag remains true
- **THEN** both detailed and compact status remain non-privileged and unavailable status retains retry guidance
