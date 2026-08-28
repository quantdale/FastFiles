## ADDED Requirements

### Requirement: Fast Search is discoverable without a hidden workflow
Fast Search SHALL be reachable from all of: the chrome Fast Search field, the sidebar Fast Search destination, the Global Ctrl+F shortcut (`search.focus`), the Home Search quick action, and the command palette. Opening Fast Search from any of those entry points SHALL show the same Search workspace and focus the query edit.

#### Scenario: Ctrl+F opens the same workspace as the sidebar
- **WHEN** the user presses Ctrl+F
- **THEN** the Search workspace is shown, Storage Analysis is hidden if it was visible, and the query edit is focused

#### Scenario: Home Search action opens Fast Search
- **WHEN** the user activates the Home Search quick action
- **THEN** the Search workspace is shown and the query edit is focused

### Requirement: Search scope remains distinct from path navigation
The Search workspace SHALL continue to expose an explicit scope selector. Path editing via the breadcrumb/address bar SHALL navigate; it SHALL NOT run a search. Typing in the Search query edit SHALL search, not navigate.

#### Scenario: Address bar commit does not search
- **WHEN** the user edits the path and commits a valid folder path
- **THEN** the shell navigates to that folder and does not open Fast Search

#### Scenario: Search query does not replace the location
- **WHEN** the user runs a Fast Search query
- **THEN** the Search workspace shows results and the browse location is restored when Search is dismissed by a later folder/Home navigation
