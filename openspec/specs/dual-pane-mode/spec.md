# dual-pane-mode Specification

## Purpose
Two independent simultaneous navigation contexts side by side, as a per-tab display mode, with toggling semantics and routing of sidebar navigation and keyboard focus to the active pane.

## Requirements
### Requirement: Two Independent Simultaneous Navigation Contexts
When dual-pane mode is active, the system SHALL display two navigation contexts side by side, each with its own current path, its own column-view instance, its own address bar, and its own back/forward history, both served by the same shared engine connection. Neither pane's navigation state SHALL be affected by navigation actions performed in the other pane.

#### Scenario: Panes show different locations independently
- **WHEN** dual-pane mode is active with the left pane showing `C:\Source` and the right pane showing `D:\Destination`
- **THEN** navigating the left pane to a different folder SHALL NOT change the right pane's current path, columns, or selection

#### Scenario: Each pane has its own address bar and history
- **WHEN** the user navigates the left pane through several folders and then the right pane through a different set of folders
- **THEN** invoking Back in the left pane SHALL step through only the left pane's own navigation history, independent of the right pane's history

### Requirement: Toggling Dual-Pane Mode On and Off
The system SHALL allow toggling dual-pane mode on and off for the currently active tab. Enabling dual-pane mode SHALL create a second navigation context initialized to the currently active tab's current path, with its own fresh (empty) history. Disabling dual-pane mode SHALL retain the previously active pane's navigation context as the tab's single context and discard the other pane's context.

#### Scenario: Enabling dual-pane mode clones the current location into a second pane
- **WHEN** the active tab is showing `C:\Users\me\Documents` in single-pane mode and the user enables dual-pane mode
- **THEN** a second pane SHALL appear showing `C:\Users\me\Documents` with its own empty history, and the first pane SHALL remain at `C:\Users\me\Documents` unaffected

#### Scenario: Disabling dual-pane mode keeps the active pane's location
- **WHEN** dual-pane mode is active with the left pane active at `C:\A` and the right pane at `C:\B`, and the user disables dual-pane mode
- **THEN** the tab SHALL return to single-pane mode showing `C:\A`, and the right pane's context SHALL be discarded

### Requirement: Coexistence With Tabs and the Primary Navigation Surface
Dual-pane mode SHALL operate as a per-tab display mode: enabling it splits the currently active tab's content area into two panes without affecting other open tabs, and each pane SHALL host a single navigation context (not its own nested tab strip) for this capability. Exactly one pane SHALL be the active pane at any time, determining which pane routes sidebar-click navigation (from `bookmarks-and-sidebar`) and receives keyboard focus; clicking directly into the other pane SHALL make it the active pane.

#### Scenario: Enabling dual-pane mode in one tab does not affect other tabs
- **WHEN** tab A is in dual-pane mode and tab B is a separate, single-pane tab
- **THEN** tab B SHALL continue to display exactly one navigation context, unaffected by tab A's dual-pane state

#### Scenario: Sidebar navigation targets the active pane
- **WHEN** dual-pane mode is active and the left pane is the active pane
- **THEN** clicking an entry in the sidebar SHALL navigate the left pane, leaving the right pane's current path unchanged

#### Scenario: Clicking into the inactive pane makes it active
- **WHEN** dual-pane mode is active and the right pane is currently inactive
- **THEN** clicking anywhere within the right pane's navigation surface SHALL make the right pane the active pane, so that subsequent sidebar clicks and keyboard input route to it
