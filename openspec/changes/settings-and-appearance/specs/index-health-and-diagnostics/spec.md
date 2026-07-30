## ADDED Requirements

### Requirement: Per-Volume Index Status Display
The system SHALL display, for each volume configured for indexing, exactly one of five status values derived from `FastFilesEngine`'s privileged-connection state and `filesystem-index-store`'s per-volume scan/reconciliation state: `Fully Indexed`, `Currently Indexing`, `Partially Indexed`, `Unavailable`, or `Needs Reconciliation`. This status SHALL be derived from existing engine and index-store state and SHALL NOT be tracked as new, independently-persisted state.

#### Scenario: Fully indexed volume is displayed as such
- **WHEN** a volume has completed scanning with no pending reconciliation and the privileged connection is in the `Active` state
- **THEN** the system SHALL display that volume's status as `Fully Indexed`

#### Scenario: Volume undergoing initial scan is displayed as currently indexing
- **WHEN** a volume's initial scan or a post-reconnection catch-up is actively in progress
- **THEN** the system SHALL display that volume's status as `Currently Indexing`

#### Scenario: Volume with an incomplete configured scope is displayed as partially indexed
- **WHEN** some but not all of a volume's currently-included directory subtrees have completed scanning (e.g., immediately after an include-rule change)
- **THEN** the system SHALL display that volume's status as `Partially Indexed`

#### Scenario: Volume is displayed as unavailable when the privileged connection is down or the volume is unreachable
- **WHEN** the privileged connection is not in the `Active` state, or the volume itself is not currently reachable (e.g., a removed external drive)
- **THEN** the system SHALL display that volume's status as `Unavailable`

#### Scenario: Volume needing catch-up is displayed as needing reconciliation
- **WHEN** the index store detects a mismatch requiring a reconciliation sweep (e.g., missed change-journal events after downtime)
- **THEN** the system SHALL display that volume's status as `Needs Reconciliation`

#### Scenario: Status explains a missing search result
- **WHEN** a user views a volume whose status is not `Fully Indexed`
- **THEN** the displayed status SHALL be sufficient for the user to understand that a search result on that volume may be missing or stale as a result

### Requirement: Status Precedence When Multiple Conditions Apply
When more than one status condition could apply to the same volume simultaneously, the system SHALL resolve to a single headline status using a fixed precedence order — `Unavailable`, then `Currently Indexing`, then `Needs Reconciliation`, then `Partially Indexed`, then `Fully Indexed` — while still making the other applicable conditions visible in a per-volume detail view.

#### Scenario: Unavailable outranks an in-progress scan
- **WHEN** a volume is both mid-scan for a newly included subtree and its privileged connection has just dropped
- **THEN** the system SHALL display the headline status as `Unavailable`, with the in-progress scan condition visible in the per-volume detail view

### Requirement: Diagnostic Logging of Indexing Conditions
The system SHALL log, locally and per-user, diagnostic entries for indexing errors, inaccessible directories encountered during scanning, volume state transitions, and database problems, sufficient to explain why a volume is not fully indexed or why a specific path was skipped.

#### Scenario: Inaccessible directory is logged
- **WHEN** a directory cannot be scanned because the current user's or the service's access is denied
- **THEN** the system SHALL record a diagnostic log entry identifying the path and the reason it was skipped

#### Scenario: Database problem is logged
- **WHEN** the durable index store encounters an error (e.g., a write failure or detected corruption)
- **THEN** the system SHALL record a diagnostic log entry describing the problem without crashing the application

### Requirement: Diagnostic Logs Never Contain File Content
Diagnostic logs SHALL contain only metadata (paths, error reasons, volume/connection state, timestamps) and SHALL NEVER contain the contents of any indexed or scanned file.

#### Scenario: Logging an indexing error does not capture file content
- **WHEN** an indexing error occurs while processing a specific file
- **THEN** the resulting log entry SHALL reference that file only by path and error metadata, and SHALL contain no portion of the file's contents

### Requirement: Redacted Diagnostic Bundle Export
The system SHALL allow the user to export a diagnostic bundle for sharing with support, and SHALL default that export to an aggregated/redacted form (counts and directory-structure summaries rather than literal paths or filenames). Including literal paths or filenames in an exported bundle SHALL require a separate, explicit opt-in at export time.

#### Scenario: Default export omits literal paths
- **WHEN** the user exports a diagnostic bundle without selecting the "include literal paths" option
- **THEN** the exported bundle SHALL present error counts and directory-structure summaries without literal path or filename text

#### Scenario: User explicitly opts into literal paths
- **WHEN** the user explicitly selects the "include literal paths" option before exporting
- **THEN** the exported bundle SHALL include literal paths, and the system SHALL still exclude all file content, consistent with the content-exclusion requirement above

### Requirement: Pause and Resume Indexing
The system SHALL allow the user to pause and later resume indexing, globally or for a specific volume, by sending a control-plane request to `FastFilesEngine`, and SHALL reflect the resulting state through the per-volume status display.

#### Scenario: User pauses indexing for a volume
- **WHEN** the user chooses to pause indexing for a specific volume
- **THEN** the system SHALL send a pause request to `FastFilesEngine` and the volume's status display SHALL reflect that indexing is paused

#### Scenario: User resumes paused indexing
- **WHEN** the user chooses to resume indexing that was previously paused
- **THEN** the system SHALL send a resume request to `FastFilesEngine` and indexing activity for the affected scope SHALL continue from where it left off, without restarting from zero

### Requirement: Enable and Disable Indexing
The system SHALL allow the user to enable or disable indexing entirely, or for a specific volume, by sending a control-plane request to `FastFilesEngine`, distinct from pausing (disabling stops indexing for that scope until explicitly re-enabled, rather than a temporary suspension).

#### Scenario: User disables indexing for a volume
- **WHEN** the user disables indexing for a specific volume
- **THEN** the system SHALL send a disable request to `FastFilesEngine`, that volume SHALL no longer be scanned or watched, and its status display SHALL indicate indexing is disabled rather than merely paused

#### Scenario: User re-enables a disabled volume
- **WHEN** the user re-enables indexing for a previously disabled volume
- **THEN** the system SHALL send an enable request to `FastFilesEngine` and that volume SHALL become eligible for scanning again

### Requirement: Adding a Newly Detected Volume to Indexing
The system SHALL treat any volume `FastFilesEngine` observes that has no matching entry in the persisted volume selection as pending a user decision, SHALL surface this condition to the user, and SHALL allow the user to add that volume to indexing without restarting the application.

#### Scenario: Newly attached volume prompts the user
- **WHEN** a volume with no existing entry in the persisted indexing configuration becomes available
- **THEN** the system SHALL surface a pending-decision indication for that volume to the user

#### Scenario: User adds the newly detected volume to indexing
- **WHEN** the user chooses to add a pending, newly detected volume to indexing
- **THEN** the system SHALL persist that volume's inclusion in the indexing configuration and SHALL notify `FastFilesEngine` to begin scanning it, without requiring an application or service restart
