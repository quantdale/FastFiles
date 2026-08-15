## Purpose

FastFiles exposes its core custom-rendered navigation and search surfaces —
Column View, Home / Quick Access, and the Search result list — as real
Microsoft UI Automation elements through the standard `WM_GETOBJECT` /
`UiaReturnRawElementProvider` mechanism, so assistive technology (Narrator,
Accessibility Insights, any UIAutomationClient) and FastFiles' own
`verify/uia-driver` discover, identify, focus, select, invoke, and scroll the
same semantics a user can see and operate. Provider semantics are projected
from the real render/interaction state; there is no parallel
accessibility-only state model and no pixel-based automation.

## ADDED Requirements

### Requirement: Custom surfaces answer WM_GETOBJECT with a real fragment root
The main window SHALL answer `WM_GETOBJECT` (`UiaRootObjectId`) with a
fragment root provider via `UiaReturnRawElementProvider`. The root SHALL be
`ControlType` Window with the live window title as Name and a stable
`AutomationId` (`MainWindow`), and SHALL expose its native child controls
exactly as before. Direct2D-rendered content SHALL have no provider work
when no UIA client is attached.

#### Scenario: The main window exposes a UIA root
- **WHEN** a UIA client queries the `FastFilesMainWindow` element
- **THEN** it has Name "FastFiles", ControlType Window, and its children
  include both the native chrome controls and the custom Column View surface

#### Scenario: No client, no work
- **WHEN** no UIA client is attached
- **THEN** painting, navigation, and search performance are unchanged
  (no per-frame provider work, no provider allocation)

### Requirement: Column View exposes a semantic hierarchy
The Column View surface SHALL expose, per pane, one column element per
rendered column and one item element per file/folder row. Panes and columns
SHALL expose meaningful Name, AutomationId, ControlType (Pane, List,
ListItem), `IsKeyboardFocusable`, `HasKeyboardFocus`, `BoundingRectangle`,
`IsOffscreen`, and `IsEnabled`. Column elements SHALL implement the
Selection pattern (multi-select allowed, not required) reflecting the real
`SelectionModel`; items SHALL implement SelectionItem, Invoke, and ScrollItem
patterns wired to the same logic used by mouse/keyboard input.

#### Scenario: Items are discoverable
- **WHEN** a directory is open
- **THEN** its rows are ListItems with their display names, and selecting an
  item through UIA updates the real selection state

#### Scenario: Focus is real focus
- **WHEN** keyboard navigation moves the column focus
- **THEN** the focused item's `HasKeyboardFocus` reflects the new focus and
  a focus-changed event is raised

#### Scenario: Scroll is the real scroll
- **WHEN** a client queries the pane's Scroll pattern or calls
  ScrollIntoView on an item
- **THEN** the reported position derives from the real viewport offsets and
  the scroll uses the same clamp math as the wheel/arrow implementation

### Requirement: Dual panes never collide
With dual-pane active, each pane SHALL be a distinct element with stable
pane-scoped identity. Identical folders open in both panes, Home in one pane
and filesystem in the other, and pane switches SHALL all keep automation
targets bound to their original pane; an automation action on an element
SHALL never affect the same-index element of the other pane.

#### Scenario: Same folder in both panes
- **WHEN** both panes show the same folder and a client selects item 3 in
  pane 1
- **THEN** only pane 1's item 3 is selected

### Requirement: Home is semantically exposed
Home SHALL expose its sections (Pinned, Recent folders, Recent searches,
Common locations, Quick actions) as discoverable groups whose headings are
not selectable items. Home items SHALL expose Name, focus, enabled state,
bounds, and Invoke routed through the existing Home activation path.
Unavailable known folders SHALL remain visible with `IsEnabled=false` and no
Invoke pattern. "Clear recent folders", "Clear recent searches", and Quick
actions SHALL be Buttons with Invoke. Home data refresh SHALL raise
structure events and invalidate stale elements.

#### Scenario: Disabled known folder
- **WHEN** a known folder is unavailable
- **THEN** its element is present, `IsEnabled=false`, and Invoke is
  unavailable

#### Scenario: Home activation parity
- **WHEN** a Home item is activated through UIA Invoke, Enter, or a mouse
  click
- **THEN** the same activation path runs and produces the same navigation,
  search, or command

### Requirement: Search results are semantically exposed without breaking virtualization
Search result rows SHALL be discoverable UIA elements exposing filename,
containing location, type, logical size (the search model's size — the data
pipeline SHALL NOT be widened for accessibility), modified time, selected
and focus state, bounds, and offscreen state. Activation SHALL use the real
search-to-navigation path. The native virtual list SHALL remain the input
and virtualization owner: no per-result HWND, no per-result D2D resource, no
layout pass per property query, no synchronous filesystem access, and no
permanent COM object per result until a client queries it. Replacing the
result set SHALL raise a structure-changed event and invalidate stale
result elements.

#### Scenario: Results after a query
- **WHEN** a query completes with results
- **THEN** each result is a ListItem with its name and metadata, selection
  state matches the list, and activating one navigates like double-click

#### Scenario: Thousands of results
- **WHEN** a query returns several thousand results
- **THEN** UIA enumeration stays bounded (on-demand lightweight providers)
  and the UI remains responsive

### Requirement: Stale providers fail cleanly
After navigation, Home refresh, or result replacement, an element that no
longer exists SHALL fail its property queries with `UIA_E_ELEMENTNOTAVAILABLE`
and SHALL never operate on the element that now occupies the same row index.
Provider lifetime SHALL be safe when the window is destroyed while clients
still hold elements (no use-after-free, no crash).

#### Scenario: Stale column item
- **WHEN** a client holds an item element, the user navigates away, and the
  client queries the old element
- **THEN** the query fails cleanly and no new state is mutated

### Requirement: Events are semantic
Focus, selection, structure (navigation, Home refresh, result replacement,
dual-pane toggles), and Invoke events SHALL be raised on the corresponding
semantic changes, never on paint ticks. Selection events SHALL use the
SelectionItem add/remove and Selection invalidated events.

#### Scenario: Selection events
- **WHEN** selection membership changes
- **THEN** the owning column raises a selection-invalidated event and the
  affected items raise element-added/removed events

### Requirement: Geometry is physical screen coordinates
`BoundingRectangle` SHALL be in physical screen pixels and match the
rendered geometry at any DPI, including PerMonitorV2 scaling changes and
window moves/resizes. `IsOffscreen` SHALL be true exactly when an element is
completely outside the visible viewport.

#### Scenario: DPI change
- **WHEN** the window moves to a monitor with a different scale
- **THEN** element bounds agree with the new rendered geometry and
  hit-testing

### Requirement: Threading and COM safety
All provider calls SHALL arrive on the UI thread, take only bounded locks,
never perform synchronous filesystem I/O, never block on search/index/
preview threads, and never `SendMessage` to the owning window. Provider
action methods and event notifications SHALL run after the underlying
mutation releases its lock so UIA callbacks cannot deadlock.

#### Scenario: Reentrancy-safe mutations
- **WHEN** a UIA callback re-enters the provider while a view mutation is
  being processed
- **THEN** the provider's state access takes bounded locks only and the
  mutation's event notifications run after the mutation released its lock,
  so no deadlock or blocking wait occurs

### Requirement: Security boundary
Providers SHALL expose only information already visible to the interactive
user. File content, privileged service internals, raw MFT bytes, signing
information, and hidden diagnostic paths SHALL NOT be exposed. No change to
the privileged service or engine security protocol is required.

#### Scenario: No content leakage
- **WHEN** a client queries any provider property
- **THEN** the result is derived from UI-visible metadata only
