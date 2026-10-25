## Purpose

Keep filesystem commands bound to the user's visible selection across browsing, search and storage workspaces.

## ADDED Requirements

### Requirement: Commands preserve visible target ownership

Item commands SHALL use the selection of the visible workspace, including applicability classification. They SHALL NOT fall back to a hidden browsing selection when that selection is empty, invalid, synthetic or unavailable. Commands requiring a browsing destination or inline browsing editor SHALL be unavailable in alternate workspaces unless explicitly supported there.

#### Scenario: Storage differs from browse selection
- **WHEN** browsing selected A and Storage selects B
- **THEN** Copy Path, Cut, Open and Delete target B through existing operation handlers, never A

#### Scenario: Target disappears
- **WHEN** an explicit selected target no longer exists
- **THEN** the command is rejected without invoking a handler on the hidden browse selection

### Requirement: Category summaries cannot become filesystem targets

Category summaries SHALL NOT be converted into filesystem paths or support filesystem mutation commands. Activation SHALL refine storage analysis or remain a non-file action.

#### Scenario: Category label matches real sibling
- **WHEN** a category named Images is selected and a sibling file named Images exists
- **THEN** category actions cannot open, cut or delete that sibling
