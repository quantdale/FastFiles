## ADDED Requirements

### Requirement: Sidebar product destinations for Fast Search and Storage Analysis
The navigation sidebar SHALL show persistent Fast Search and Storage Analysis destinations immediately after Home and before the Drives, Known Folders, and Bookmarks sections. Activating Fast Search SHALL open the Search workspace without treating the destination as a filesystem path. Activating Storage Analysis SHALL open the Storage Analysis workspace without treating the destination as a filesystem path. Each destination SHALL show a selected state while its workspace is the active content workspace. These destinations SHALL remain reachable from the collapsed sidebar rail via tooltip-labeled icons.

#### Scenario: Clicking sidebar Fast Search
- **WHEN** the user clicks the sidebar Fast Search entry
- **THEN** Fast Search becomes the content workspace and the entry shows the selected state

#### Scenario: Clicking sidebar Storage Analysis
- **WHEN** the user clicks the sidebar Storage Analysis entry
- **THEN** Storage Analysis becomes the content workspace and the entry shows the selected state

#### Scenario: Tool destinations never become This PC
- **WHEN** Fast Search or Storage Analysis is activated
- **THEN** the shell SHALL NOT navigate to the empty-path This PC root and SHALL NOT call `ShowUnavailableLocation` for that activation
