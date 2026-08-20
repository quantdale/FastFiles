# ui-automation delta — harden-selection-model-semantics

## MODIFIED Requirements

### Requirement: Column View exposes a semantic hierarchy
The Column View surface SHALL expose, per pane, one column element per rendered column and one item element per file/folder row. Panes and columns SHALL expose meaningful Name, AutomationId, ControlType (Pane, List, ListItem), `IsKeyboardFocusable`, `HasKeyboardFocus`, `BoundingRectangle`, `IsOffscreen`, and `IsEnabled`. Column elements SHALL implement the Selection pattern (multi-select allowed, not required) reflecting the real `SelectionModel`; items SHALL implement SelectionItem, Invoke, and ScrollItem patterns wired to the same logic used by mouse/keyboard input. UIA selection properties SHALL reflect modifier-driven native input immediately after the real model changes and SHALL NOT maintain a parallel accessibility-only selection cache.

#### Scenario: Items are discoverable
- **WHEN** a directory is open
- **THEN** its rows are ListItems with their display names, and selecting an item through UIA updates the real selection state

#### Scenario: Native selection input is observable through UIA
- **WHEN** plain-click, Ctrl-click, Shift-click, or Ctrl+A changes the real Column View selection
- **THEN** `Selection.GetSelection()` and each affected item's `SelectionItem.IsSelected` SHALL report exactly the resulting real selection membership

#### Scenario: UIA selection actions preserve real model semantics
- **WHEN** a UIA client calls `SelectionItem.Select`, `AddToSelection`, or `RemoveFromSelection`
- **THEN** the same production selection model SHALL be mutated, the owning column's `Selection.GetSelection()` SHALL agree with the result, and unrelated items SHALL retain their prior membership

#### Scenario: Focus is real focus
- **WHEN** keyboard navigation moves the column focus
- **THEN** the focused item's `HasKeyboardFocus` reflects the new focus and a focus-changed event is raised

#### Scenario: Scroll is the real scroll
- **WHEN** a client queries the pane's Scroll pattern or calls ScrollIntoView on an item
- **THEN** the reported position derives from the real viewport offsets and the scroll uses the same clamp math as the wheel/arrow implementation

### Requirement: Dual panes never collide
With dual-pane active, each pane SHALL be a distinct element with stable pane-scoped identity. Identical folders open in both panes, Home in one pane and filesystem in the other, and pane switches SHALL all keep automation targets bound to their original pane; an automation action or native selection mutation on one pane SHALL never affect the same-index element or existing selection state of the other pane.

#### Scenario: Same folder in both panes
- **WHEN** both panes show the same folder and a client selects item 3 in pane 1
- **THEN** only pane 1's item 3 is selected

#### Scenario: Ctrl+A is observable as pane-local
- **WHEN** both panes have known selection state, pane 1 is active, and the user invokes Ctrl+A
- **THEN** UIA SHALL report every listed item selected in pane 1 while pane 2's previously selected membership remains unchanged

### Requirement: Events are semantic
Focus, selection, structure (navigation, Home refresh, result replacement, dual-pane toggles), and Invoke events SHALL be raised on the corresponding semantic changes, never on paint ticks. Selection events SHALL use the SelectionItem add/remove and Selection invalidated events, and event payload/state SHALL agree with the same selection membership exposed through Selection/SelectionItem after the mutation.

#### Scenario: Selection events
- **WHEN** selection membership changes through mouse, keyboard, or UIA SelectionItem action
- **THEN** the owning column raises a selection-invalidated event and the affected items raise element-added/removed events consistent with the resulting real selection state
