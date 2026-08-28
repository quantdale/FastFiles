## ADDED Requirements

### Requirement: Home quick actions include Storage Analysis
Home's Quick actions strip SHALL include Analyze Storage in addition to New Tab, Search, Open Path, and Settings. Activating Analyze Storage SHALL invoke `storage.analyze`. The action count is five.

#### Scenario: Analyze Storage is present on Home
- **WHEN** Home is shown with quick actions enabled
- **THEN** the Quick actions section contains five items including Analyze Storage

#### Scenario: Analyze Storage from Home opens the workspace
- **WHEN** the user activates the Analyze Storage Home action
- **THEN** the Storage Analysis workspace is shown
