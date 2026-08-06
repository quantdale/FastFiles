# settings-ui Specification

## Purpose
The settings surface: local JSON persistence with resilient loading, indexed-volume selection, include/exclude rules, search/theme/navigation/preview/storage-analysis preferences, keyboard-shortcut customization, and reset-to-defaults.

## Requirements
### Requirement: Local File-Based Settings Persistence
The system SHALL persist all user-configurable settings to a local, per-Windows-user JSON file under the user's local application-data folder (e.g. `%LOCALAPPDATA%\FastFiles\settings.json`), rather than the Windows Registry. `FastFiles` (the UI process) SHALL be the sole writer of this file.

#### Scenario: Settings survive an application restart
- **WHEN** the user changes any setting and then closes and relaunches `FastFiles`
- **THEN** the changed value SHALL be read back from the local settings file and reflected in the UI, without requiring the user to reconfigure it

#### Scenario: Settings file is human-inspectable
- **WHEN** a user or support engineer opens the settings file in a text editor
- **THEN** the file SHALL be valid, readable JSON reflecting the current effective configuration, with no proprietary or binary encoding

### Requirement: Resilient Settings Loading
The system SHALL validate each top-level settings section independently on load and SHALL fall back to that section's defaults if it is missing, malformed, or fails validation, rather than discarding the entire settings file or failing to start.

#### Scenario: One malformed section does not erase the rest
- **WHEN** the settings file has a syntactically valid but semantically invalid value in one section (e.g., an unrecognized theme name) while other sections are valid
- **THEN** only that section SHALL fall back to its default value, all other sections SHALL load as configured, and the application SHALL start normally

#### Scenario: Corrupt settings file is preserved, not silently lost
- **WHEN** the settings file cannot be parsed at all (e.g., invalid JSON)
- **THEN** the system SHALL preserve the original file under a backup name, load all sections from defaults, and record a diagnostic log entry describing the fallback

### Requirement: Indexed Volume Selection
The system SHALL allow the user to view all volumes discoverable for indexing and to select which volumes are included in indexing, and SHALL persist this selection.

#### Scenario: User excludes a volume from indexing
- **WHEN** the user deselects a currently-indexed volume in settings
- **THEN** the system SHALL persist the updated volume selection and SHALL communicate the change so that volume is no longer scanned or watched

#### Scenario: Newly attached volume is not indexed until selected
- **WHEN** a volume with no existing entry in the persisted volume selection is present
- **THEN** the system SHALL NOT index that volume until the user explicitly adds it (see the newly-detected-volume requirement in `index-health-and-diagnostics`)

### Requirement: Directory Include/Exclude Rules
The system SHALL allow the user to define, per indexed volume, an ordered list of directory include/exclude rules matched by absolute directory path prefix (subtree match) against the canonical path of an indexed item, with the most specific (longest) matching rule taking precedence over less specific overlapping rules.

#### Scenario: Excluding a subtree removes it from future indexing
- **WHEN** the user adds an exclude rule for a directory path
- **THEN** the system SHALL persist the rule and SHALL communicate the change so that directory subtree is no longer scanned, watched, or represented in the index going forward

#### Scenario: More specific rule wins over a broader overlapping rule
- **WHEN** a broader include rule and a more specific exclude rule both match the same path (e.g., volume included overall, one subfolder excluded)
- **THEN** the system SHALL apply the more specific (longer-prefix) rule to that path

### Requirement: Indexing Configuration Applied Without Restart
The system SHALL apply changes to indexed-volume selection and include/exclude rules by notifying `FastFilesEngine` over its existing control-plane connection, and SHALL NOT require restarting `FastFiles`, `FastFilesEngine`, or `FastFilesIndexSvc` for the change to take effect.

#### Scenario: Rule change takes effect while the application keeps running
- **WHEN** the user saves a change to indexing configuration
- **THEN** `FastFilesEngine` SHALL be notified of the change and SHALL re-evaluate its scanning/watching scope accordingly without any process restart

### Requirement: Search Preference Settings
The system SHALL allow the user to configure search-related preferences, including the default search scope and local search-history retention (including clearing history), and SHALL persist these preferences for use by the search experience.

#### Scenario: Default search scope is applied to new searches
- **WHEN** the user sets a default search scope in settings and later starts a new search
- **THEN** the new search SHALL initialize with the configured default scope

#### Scenario: User clears search history
- **WHEN** the user chooses to clear search history from settings
- **THEN** the system SHALL remove all locally stored search history entries

### Requirement: Appearance Theme Selection Setting
The system SHALL allow the user to select an application theme of Light, Dark, or Follow-System, and SHALL persist this selection for the `theming` capability to apply.

#### Scenario: User selects an explicit theme
- **WHEN** the user selects "Dark" (or "Light") in settings
- **THEN** the system SHALL persist that explicit selection and the application SHALL apply it regardless of the current OS theme

#### Scenario: User selects Follow-System
- **WHEN** the user selects "Follow-System" in settings
- **THEN** the system SHALL persist that selection and the application's theme SHALL track the current Windows light/dark setting

### Requirement: Navigation Preference Settings
The system SHALL allow the user to configure navigation-related preferences, including the default startup location and whether to restore the previous session's open locations on launch, and SHALL persist these preferences.

#### Scenario: Application restores previous session on launch
- **WHEN** the "restore previous session" preference is enabled and the user relaunches `FastFiles`
- **THEN** the application SHALL reopen the locations that were open when it was last closed

#### Scenario: Application opens a fixed default location on launch
- **WHEN** the "restore previous session" preference is disabled and a default startup location is configured
- **THEN** the application SHALL open at the configured default location on launch

### Requirement: Keyboard Shortcut Customization Surface
The system SHALL provide a settings surface that displays the current keyboard shortcut bindings, allows the user to rebind them, detects and surfaces conflicts between bindings, and allows resetting bindings to their defaults, operating against the shortcut data model owned by the `keyboard-shortcuts` capability without redefining that model.

#### Scenario: User rebinds a shortcut
- **WHEN** the user assigns a new key combination to an existing command in the shortcut settings surface
- **THEN** the system SHALL persist the new binding and the command SHALL subsequently be invoked by the new key combination

#### Scenario: Conflicting binding is surfaced, not silently applied
- **WHEN** the user assigns a key combination that is already bound to a different command
- **THEN** the system SHALL surface the conflict to the user before the rebinding is committed, rather than silently overwriting the existing binding

#### Scenario: User resets shortcuts to defaults
- **WHEN** the user chooses to reset keyboard shortcuts to their defaults
- **THEN** all customized bindings SHALL be discarded and the default bindings SHALL be restored

### Requirement: Preview Behavior Settings
The system SHALL allow the user to configure file-preview behavior, including enabling or disabling the preview pane, and a maximum file size above which files are not automatically previewed.

#### Scenario: Preview pane is disabled
- **WHEN** the user disables the preview pane in settings
- **THEN** the application SHALL stop rendering the preview pane for selected items until the setting is re-enabled

#### Scenario: Large file is not auto-previewed
- **WHEN** a selected file's size exceeds the configured maximum auto-preview size
- **THEN** the system SHALL NOT automatically render a preview for that file

### Requirement: Storage-Analysis Behavior Settings
The system SHALL allow the user to view and customize file-type/extension category definitions used by storage-analysis breakdown-by-category views, and SHALL persist these customizations.

#### Scenario: User edits a category's extension list
- **WHEN** the user adds or removes an extension from a storage-analysis category in settings
- **THEN** the system SHALL persist the updated category definition for use by subsequent storage-analysis breakdown views

### Requirement: Reset Settings to Defaults
The system SHALL allow the user to reset all settings to their default values in a single action, without affecting persisted index data.

#### Scenario: User resets all settings
- **WHEN** the user confirms a "reset to defaults" action
- **THEN** the system SHALL rewrite the settings file with default values for every section, and SHALL NOT delete or modify any persisted index data
