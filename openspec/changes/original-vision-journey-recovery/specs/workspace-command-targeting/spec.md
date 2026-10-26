## Purpose

Keep filesystem commands bound to the user's visible selection across browsing, search and storage workspaces.

## ADDED Requirements

### Requirement: Treemap preserves partial facts through presentation

Treemap folder aggregate leaves SHALL retain known logical and allocated lower bounds and their partial or pending state when parent totals are recomputed. Represented containers SHALL derive their own totals from retained children, including empty filtered results. Allocation completeness SHALL remain independent of logical enumeration completeness. Positive partial sizes SHALL retain visible lower-bound text, and unknown allocation SHALL NOT become zero or exact logical bytes. Logical size and coverage feedback SHALL be available without requiring pointer hover on sufficiently large tiles.

#### Scenario: Known file with an unbrowsed descendant
- **WHEN** a folder aggregate contains a known 3 MiB subtotal and a missing descendant listing
- **THEN** actual root recomputation preserves the subtotal and partial state, exact parent percentages remain unavailable, and size text identifies the known lower bound
- **AND** observed allocation counts do not imply complete allocation for missing descendants

#### Scenario: Empty category result
- **WHEN** category filtering removes every represented child
- **THEN** the root does not retain the previous unfiltered logical or allocated totals

#### Scenario: Pointer away from a settled tile
- **WHEN** a sufficiently large settled tile is visible without pointer hover
- **THEN** its name and logical size remain readable and the existing reserved status row explains that data coverage may be partial

### Requirement: Recursive storage ranking preserves source scope and target identity

All-category Largest Files SHALL rank at most100 files by logical size from the selected folder and known subfolders. Directory exclusion and component-bounded scope matching SHALL precede the ranking cap. Indexed records SHALL state that coverage is unverified; published/browsed fallback SHALL state partial coverage, including while service-connected. No successful directory listing, volume ID or capped result sum SHALL be presented as proof of recursive completeness.

Each ranked row SHALL retain its source containing folder for visible location, UIA identity, selection restoration, file operations and canonical reveal. Equal basenames and separate hard-link paths SHALL not retarget each other. Unknown allocated size SHALL remain unknown. Configurable category refinements SHALL remain explicitly immediate-folder; sorting ranked rows SHALL describe sorting only the retained set. Treemap SHALL continue to use its hierarchical data.

#### Scenario: A larger descendant beats an immediate child
- **WHEN** a known nested file is larger than an immediate file and an even larger same-prefix sibling lies outside the selected scope
- **THEN** the descendant ranks before the immediate file, the sibling is excluded before admission, and reveal/Copy Path target the descendant's actual containing folder

#### Scenario: Indexed query is unavailable or empty
- **WHEN** indexed ranking cannot supply rows while successful published/browsed listings contain known files
- **THEN** a cancellable worker ranks those known files and labels the result partial without claiming missing subfolders are empty

#### Scenario: Workspace changes before completion
- **WHEN** a ranking completion arrives after hide, scope change, category/mode replacement or a newer request
- **THEN** it cannot replace the current rows or supply actions to another workspace

### Requirement: Settings completion controls fit the real dialog

Settings SHALL reserve its intended DPI-scaled client area in addition to the actual non-client frame. OK and Cancel SHALL have positive, fully visible bounds inside the dialog, and visible Cancel SHALL remain pointer-reachable without changing saved settings.

#### Scenario: Settings at 120 DPI
- **WHEN** Settings is opened on the measured120DPI desktop
- **THEN** caption and borders do not consume the footer's client layout and both completion controls fit the visible frame

### Requirement: Command choices remain visibly above their workspace

The real native palette edit and command list SHALL paint above Search, Storage and browsing controls. Native child creation SHALL preserve sibling paint clipping as well as z-order. A client click outside the palette SHALL dismiss it without triggering the underlying workspace action.

#### Scenario: Palette over Storage
- **WHEN** the palette opens over Storage and the underlying controls repaint
- **THEN** its visible command choices remain legible and reachable

#### Scenario: Outside dismissal
- **WHEN** the user clicks the navigation region outside the palette
- **THEN** the palette closes and the current Storage workspace remains active

### Requirement: Accessibility follows workspace presentation

Browse and Home semantic descendants SHALL be unavailable while another workspace replaces their presentation. Retained elements SHALL remain unavailable after restoration; new Browse elements SHALL describe the restored presentation. Native Search/Storage controls and active Treemap descendants SHALL remain reachable.

#### Scenario: Search replaces Home
- **WHEN** Search replaces Home while a client retains a Home element
- **THEN** the retained element cannot invoke or expose hidden Home content, and closing Search exposes a fresh Home fragment

#### Scenario: Storage replaces Browse
- **WHEN** Storage replaces browsing columns
- **THEN** hidden browsing items cannot receive focus or actions through UIA, while Storage list and Treemap retain their real semantics

### Requirement: Accessibility actions retain original intent through dispatch

Native list and Home Invoke/Clear actions SHALL validate the original item identity and owner/fragment lifetime on the owning UI thread before invoking the canonical mutation. Failed dispatch SHALL NOT execute shell mutations on a foreign caller thread. Teardown SHALL invalidate accessibility lifetime before draining pending messages.

#### Scenario: Home changes before dispatch
- **WHEN** a Home item changes or its fragment retires between provider resolution and owner-thread action
- **THEN** the action fails without invoking its replacement or reviving the retired fragment

#### Scenario: Search changes before dispatch
- **WHEN** a search row is replaced while its action waits for the UI thread
- **THEN** identity validation rejects the action instead of opening the new occupant

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

#### Scenario: Category refinement enters Treemap
- **WHEN** a category summary refines the current list and the user enters Treemap
- **THEN** Treemap retains the same category scope instead of showing unfiltered siblings

### Requirement: Storage list exposes real semantic rows

Storage lists SHALL expose their rendered rows through semantic UI Automation with names, metadata, physical geometry, selection, scroll and activation actions backed by the same production model and mutations. Runtime identities SHALL be distinct from Search and stale rows SHALL fail rather than retarget. Client-held providers SHALL become safe unavailable elements after owner destruction.

#### Scenario: Storage file selection through accessibility
- **WHEN** an accessibility client selects a rendered file row and invokes Copy Path
- **THEN** the copied path identifies that visible file and sorting preserves the selected identity

#### Scenario: Synthetic category activation
- **WHEN** a client invokes a category summary
- **THEN** the same category refinement as pointer activation occurs without constructing a filesystem path

#### Scenario: Replaced row
- **WHEN** a client retains a provider while its row is replaced or its owner destroyed
- **THEN** access reports element unavailable and never targets a new occupant
