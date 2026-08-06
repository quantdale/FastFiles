# tabs Specification

## Purpose
Per-tab navigation state, opening/closing/switching/reopening tabs without disturbing other tabs, and a shared engine connection and index snapshot.

## Requirements
### Requirement: Independent Per-Tab Navigation State
The system SHALL represent each open tab as an independent navigation context holding its own current path, its own column-view instance state (populated columns, per-column selection and scroll position), its own address bar mode (breadcrumb or editable-text), and its own back/forward history. All open tabs SHALL share the same underlying engine connection and index snapshot (per the application's single-connection architecture) without each tab owning a separate connection.

#### Scenario: Two tabs show different locations simultaneously
- **WHEN** tab A is showing `C:\Users\me\Documents` and tab B is showing `D:\Projects`
- **THEN** both tabs SHALL remain independently displayed with their own current path, columns, and selection, with neither affecting the other

#### Scenario: Per-tab state survives switching away and back
- **WHEN** the user switches from tab A to tab B and then back to tab A
- **THEN** tab A SHALL still show the same current path, column state, and selection it had before the switch

### Requirement: Opening a New Tab Never Disturbs Another Tab
The system SHALL allow opening a new tab at any time, and the newly opened tab SHALL start with its own current path (either a configured default location or a clone of the currently active tab's current path at the moment of opening) and an empty back/forward history. Opening a new tab SHALL NOT modify the current path, column state, selection, or history of any other already-open tab.

#### Scenario: Opening a new tab leaves existing tabs unchanged
- **WHEN** the user has tab A open at `C:\Users\me\Documents` and opens a new tab B
- **THEN** tab A SHALL remain at `C:\Users\me\Documents` with its history and selection unchanged, and tab B SHALL be a distinct navigation context

#### Scenario: New tab starts with empty history
- **WHEN** a new tab is opened
- **THEN** that tab's back and forward history SHALL both be empty until the tab itself records its own folder-change navigation events

### Requirement: Closing a Tab
The system SHALL allow closing any open tab other than the sole remaining tab (at least one tab SHALL always remain open). Closing a tab SHALL discard that tab's live navigation state (current path, column state, history) and SHALL record it as the most recently closed tab for reopening.

#### Scenario: Closing a non-last tab removes only that tab
- **WHEN** three tabs are open and the user closes the second tab
- **THEN** the first and third tabs SHALL remain open and unaffected, and the second tab's navigation surface SHALL no longer be displayed

#### Scenario: Closing the last remaining tab is prevented or replaced
- **WHEN** only one tab is open and the user attempts to close it
- **THEN** the system SHALL either prevent the close action or immediately open a replacement default tab, such that at least one tab always remains open

### Requirement: Switching Between Tabs
The system SHALL allow switching which tab is active (visible and receiving keyboard focus) without altering the navigation state of any tab, including the one being switched away from.

#### Scenario: Switching tabs changes only which is displayed
- **WHEN** the user switches the active tab from tab A to tab B
- **THEN** tab B's navigation surface SHALL become visible and focused, tab A's navigation surface SHALL stop being visible, and neither tab's current path, column state, or history SHALL change as a result

### Requirement: Reopening a Closed Tab
The system SHALL maintain a record of recently closed tabs (at least the most recently closed one) sufficient to reopen a closed tab at its last current path. Reopening a closed tab SHALL create a new navigation context at that recorded path with a fresh history, not a restoration of the exact prior history or column selection state.

#### Scenario: Reopening the most recently closed tab restores its last path
- **WHEN** the user closes a tab that was showing `C:\Users\me\Downloads` and then invokes "reopen closed tab"
- **THEN** a new tab SHALL open navigated to `C:\Users\me\Downloads`

#### Scenario: Reopening a closed tab whose path no longer exists gives clear feedback
- **WHEN** the user reopens a closed tab whose recorded path has since been deleted
- **THEN** the new tab SHALL surface the standard nonexistent-path feedback rather than crashing or silently opening a blank tab with no explanation

#### Scenario: Multiple closed tabs can be reopened in most-recent-first order
- **WHEN** the user has closed two tabs in sequence and invokes "reopen closed tab" twice
- **THEN** the most recently closed tab SHALL be reopened first, followed by the one closed before it
