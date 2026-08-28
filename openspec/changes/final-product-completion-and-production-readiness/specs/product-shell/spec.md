## ADDED Requirements

### Requirement: Four mutually exclusive content workspaces
The FastFiles shell SHALL present exactly one content workspace at a time over the post-navigation `contentRect`: Browse (Column View filesystem), Home (Column View Home mode), Fast Search (`SearchPanel`), or Storage Analysis (`StorageAnalysis`). Opening Fast Search SHALL hide Storage Analysis. Opening Storage Analysis SHALL hide Fast Search. Navigating to Home or a filesystem location SHALL hide both Fast Search and Storage Analysis. Search and Storage SHALL never be represented as filesystem paths and SHALL never be passed to Win32 filesystem APIs.

#### Scenario: Search replaces browse content
- **WHEN** the user opens Fast Search while browsing a folder
- **THEN** the Search workspace occupies the content rect, the sidebar remains visible, and Column View is not the hit-test target for that rect

#### Scenario: Folder navigation leaves Search
- **WHEN** Fast Search is visible and the user activates a sidebar folder or Home
- **THEN** Search hides and the content workspace becomes Browse or Home as appropriate

#### Scenario: Search and Storage are exclusive
- **WHEN** Storage Analysis is visible and the user opens Fast Search
- **THEN** Storage Analysis hides and Fast Search is shown

### Requirement: Tab-then-toolbar chrome composition
The navigation chrome SHALL keep a 72 DIP height and SHALL compose as two rows: a tab strip with new-tab on the first row, and Back, Forward, Up, breadcrumb/path, and Fast Search on the second row. The chrome SHALL NOT permanently occupy the second-row trailing slot with a drive combo; drives remain reachable from the sidebar.

#### Scenario: Tabs occupy the first chrome row
- **WHEN** the window is shown at a normal width
- **THEN** tab pills are laid out on the top chrome row and Back/Forward/Up plus the breadcrumb occupy the lower chrome row

#### Scenario: Up is a visible chrome command
- **WHEN** the active location has a filesystem parent
- **THEN** the chrome Up button is enabled and activating it navigates to that parent using the existing `navigation.up` command

#### Scenario: Up is disabled at Home and This PC
- **WHEN** the active location is Home or the synthetic This PC root
- **THEN** the chrome Up button is disabled

### Requirement: Chrome Fast Search is an entry point
The chrome SHALL provide a persistent Fast Search field on the toolbar row. Activating it SHALL invoke the existing `search.focus` command and transfer query focus to the Search workspace query edit. The chrome field SHALL NOT maintain an independent query buffer.

#### Scenario: Clicking the chrome search field opens Fast Search
- **WHEN** the user clicks the chrome Fast Search field
- **THEN** the Search workspace is shown and the Search query edit receives focus

#### Scenario: Narrow chrome keeps search reachable
- **WHEN** the window is narrower than the chrome's existing narrow breakpoint
- **THEN** Fast Search remains visible as at least an icon-sized control and still invokes `search.focus`
