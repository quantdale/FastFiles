# command-palette Specification

## Purpose
The keyboard-first command palette over the shared command registry: fuzzy, live-updating search, eligibility indication, bound-shortcut display, and baseline command coverage.

## Requirements
### Requirement: Shared Command Registry as the Single Source of Truth
The command palette SHALL list and execute commands by reading the same shared command registry that `context-menus-and-quick-actions` and `keyboard-shortcuts` read from, so that a command is defined once and exposed through the menu, the palette, and any bound shortcut without a palette-specific redefinition of its behavior.

#### Scenario: A newly registered command appears in the palette automatically
- **WHEN** a capability area registers a new command in the shared registry, including a display name and (optionally) a default shortcut
- **THEN** the command SHALL appear in the command palette's results without any additional palette-specific registration step

#### Scenario: Palette execution matches menu execution
- **WHEN** a command is executed from the command palette
- **THEN** it SHALL invoke the exact same handler that the context menu or a bound keyboard shortcut for that command would invoke

### Requirement: Command Palette Invocation
`FastFiles` SHALL provide a way to open the command palette via a dedicated keyboard shortcut, available from anywhere within the application.

#### Scenario: Opening the palette via keyboard
- **WHEN** a user presses the command palette's bound keyboard shortcut from any view
- **THEN** the command palette overlay SHALL open, ready to accept typed input immediately

### Requirement: Fuzzy, Discoverable Command Search
The command palette SHALL match typed input against command display names using partial and out-of-order (fuzzy) matching, not only exact prefix matching, updating the displayed results as the user types.

#### Scenario: Partial, non-prefix input finds a matching command
- **WHEN** a user types a substring or an abbreviated, out-of-order fragment of a command's display name (for example, "antp" for "Analyze Storage")
- **THEN** that command SHALL appear among the results, ranked among other candidate matches

#### Scenario: Results update live as input changes
- **WHEN** a user continues typing additional characters into the palette's input field
- **THEN** the displayed result list SHALL update to reflect the refined query without requiring a manual confirm step

#### Scenario: Closely named commands remain distinguishable
- **WHEN** a user's query could plausibly match multiple similarly named commands (for example "Copy", "Copy Path", and "Copy Relative Path")
- **THEN** the results SHALL rank exact-prefix and word-boundary matches above pure subsequence matches, and each result SHALL display enough of its full name and category to be distinguishable

### Requirement: Command Palette Is Fully Executable Without the Mouse
Every step of using the command palette — opening it, searching, selecting a result, and executing it — SHALL be achievable entirely from the keyboard.

#### Scenario: End-to-end keyboard-only execution
- **WHEN** a user opens the palette via its shortcut, types a query, moves the highlighted result with the arrow keys, and presses Enter
- **THEN** the highlighted command SHALL execute, and the palette SHALL close, all without any mouse input

#### Scenario: Dismissing the palette without executing a command
- **WHEN** a user presses Escape while the command palette is open
- **THEN** the palette SHALL close without executing any command, returning focus to wherever it was before the palette opened

### Requirement: Context-Sensitive Command Eligibility
The command palette SHALL indicate when a listed command is not currently applicable (for example, an action requiring a selection when nothing is selected) rather than allowing it to be invoked and fail silently or crash.

#### Scenario: Invoking a currently inapplicable command
- **WHEN** a user selects a command from the palette whose eligibility predicate is not currently satisfied
- **THEN** the command SHALL be shown as disabled in the results list, or invoking it SHALL produce a clear, non-crashing message explaining why it cannot run right now

### Requirement: Palette Results Display Bound Shortcuts
When a command has a keyboard shortcut currently bound to it, the command palette SHALL display that shortcut alongside the command's name in the results list, sourced from the same shortcut data `keyboard-shortcuts` maintains.

#### Scenario: A command with a bound shortcut
- **WHEN** the command palette displays a result for a command that has an active keyboard shortcut binding
- **THEN** that binding SHALL be shown next to the command's name, and SHALL match exactly what `keyboard-shortcuts` would report as that command's current binding

### Requirement: Baseline Command Coverage
The command registry SHALL include, at minimum, commands for New Folder, Copy Path, Open Terminal Here, Search, Analyze Storage, Toggle Column View, Toggle Dual Pane, Refresh, and Settings, each discoverable and executable through the command palette.

#### Scenario: Baseline commands are discoverable
- **WHEN** a user opens the command palette and searches for any of the baseline commands by name
- **THEN** that command SHALL appear in the results and SHALL execute the corresponding action when selected
