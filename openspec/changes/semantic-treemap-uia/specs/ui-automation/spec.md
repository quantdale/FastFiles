# semantic-treemap-uia — ui-automation delta

## Purpose

Delta for `semantic-treemap-uia`: the canonical `ui-automation` spec covers
Column View, Home, and Search. This delta adds the Storage Analysis Treemap
— the last custom-rendered surface without a semantic projection — to the
same single fragment-root contract: real UIA elements for the Direct2D
treemap, stable identity, honest focus/selection reporting, thread-safe
property reads, UI-thread Invoke marshaling, and geometry that agrees with
the real layout and hit-testing. It also makes the live validation
requirements for the treemap explicit. No canonical requirement is
weakened.

## ADDED Requirements

### Requirement: The Storage Treemap is exposed as one semantic group inside the main fragment root
When the Storage Analysis Treemap view is the active workspace surface, the
main window's fragment root SHALL expose one Group element with Name
`Storage Treemap` and AutomationId `Storage.Treemap` containing one DataItem
element per rendered treemap tile, projected from the exact cached layout
and tree state used for rendering and hit-testing. When the treemap is not
active, the tree SHALL be exactly the pre-change tree. Native Storage
Analysis controls (radio buttons, category combo, list views) SHALL keep
their native HWND providers; the custom tree SHALL NOT fabricate duplicate
rows for native list modes. There SHALL be exactly one fragment root for the
main window, no per-tile HWNDs, and no invisible accessibility-only
windows.

#### Scenario: Treemap tiles are discoverable
- **WHEN** a UIA client queries the main window while the Treemap view is
  active
- **THEN** it finds the `Storage.Treemap` group and each visible tile as a
  DataItem whose Name is the item name, whose HelpText is the full path,
  whose FullDescription carries the canonical metadata, and whose
  BoundingRectangle is the tile's real screen rectangle

#### Scenario: The obscured Column View is not presented as visible
- **WHEN** the Treemap view is active
- **THEN** the fragment root's child traversal exposes the Treemap group
  and not a visible Column View subtree

### Requirement: Treemap tiles expose honest metadata and activation
Real tiles SHALL implement `IInvokeProvider`; the synthetic "Other small
items" catch-all tile SHALL expose no Invoke pattern. Tiles SHALL expose no
Selection or SelectionItem pattern (the treemap has no selection model;
hover is not selection). Tiles SHALL report their real keyboard-focus
state; when the treemap has no focus model, `IsKeyboardFocusable` and
`HasKeyboardFocus` SHALL be false rather than fabricated.

#### Scenario: Folder tile Invoke equals a mouse click
- **WHEN** a client invokes a folder tile through UIA
- **THEN** the treemap drills into that folder exactly as a mouse click
  would, the scope updates, and pre-invoke tile providers become stale

#### Scenario: File tile Invoke opens the browser at the containing folder
- **WHEN** a client invokes a file tile through UIA
- **THEN** the standard column-view surface opens at the file's containing
  folder with the file selected and no file operation is performed

### Requirement: Treemap identity, stale semantics, and lifetime match the established contract
Tile runtime IDs SHALL be stable per semantic identity (FRN when present,
full-path fallback) within a treemap generation and scope; layout position
and vector index SHALL NOT be identity. A client-held tile whose identity no
longer exists SHALL fail property queries with
`UIA_E_ELEMENTNOTAVAILABLE` and SHALL NOT retarget the new occupant of the
same index. A tile whose identity is unchanged while its aggregate or
layout resolves SHALL remain valid and report updated metadata and bounds.
Window destruction SHALL be safe (no use-after-free, no crash), reusing the
established owner-lifetime token.

#### Scenario: Stale tile after scope navigation
- **WHEN** a client holds a tile, the user navigates to a different scope,
  and the client queries the old tile
- **THEN** the query fails cleanly with
  `UIA_E_ELEMENTNOTAVAILABLE` and mutates nothing

#### Scenario: Aggregate resolution updates in place
- **WHEN** a `Calculating…` directory tile's aggregate resolves without an
  identity change
- **THEN** the same provider reports the updated size, percentages, bounds,
  and a resolved ItemStatus

### Requirement: Treemap provider calls are thread-safe and Invoke marshals to the UI thread
Provider property reads SHALL be bounded-lock reads of the semantic
projection, perform no filesystem I/O, no engine request, no synchronous
cross-thread window message, and no unbounded wait, whether the call
arrives on the UI thread or a UIAutomationCore worker thread. UIA Invoke
SHALL marshal the navigation action to the owning UI thread with a value
identity payload (never a raw model pointer); the UI thread SHALL re-resolve
the identity against current state and run the same navigation mutation as a
mouse click. A queued action whose identity no longer exists SHALL be
dropped safely, never retargeting a new tile.

#### Scenario: Invoke from a worker thread
- **WHEN** a UIAutomationCore worker thread invokes a tile
- **THEN** the navigation runs on the UI thread through the real click path
  and UI state is never mutated on the worker thread

### Requirement: Treemap geometry and hit-testing agree with the real layout
Tile BoundingRectangle values SHALL be physical screen pixels derived from
the cached treemap layout via the live viewport geometry and DPI scale; no
layout math SHALL be reimplemented in the provider layer.
`ElementProviderFromPoint` on the fragment root, when the treemap is active,
SHALL return the tile that the real treemap hit-test resolves for the same
point. `IsOffscreen` SHALL reflect real viewport visibility.

#### Scenario: Click-precision parity
- **WHEN** a client clicks the center of a tile's BoundingRectangle
- **THEN** the product activates exactly the tile that supplied the
  rectangle

### Requirement: Treemap events are semantic
Structure-changed events SHALL be raised on the Treemap group for treemap
open/close, scope navigation, snapshot rebuild, category-filter change, and
catch-all composition changes. An Invoked event SHALL follow a successful
UIA Invoke. No events SHALL be raised for paint, animation frames, or hover
changes.

#### Scenario: No per-frame event storm
- **WHEN** the treemap is open and only hover or animation changes
- **THEN** no UIA structure or property events are raised

### Requirement: Live treemap validation replaces the historical skip
The `windows-ui-automation-validation` capability SHALL run
`treemap-readability-click-precision` as a real isolated scenario asserting:
semantic discovery of `Storage.Treemap` and fixture tiles, proportional
tile areas, click precision from UIA-derived coordinates, folder and file
Invoke through the real navigation paths, category-filter semantics when
deterministically available, resize behavior, many-tiny-file layout honesty
(no overlapping or out-of-container rectangles), and filesystem
non-mutation via a before/after manifest. Pixel/OCR validation SHALL NOT be
used.

#### Scenario: Treemap scenario is isolated and repeatable
- **WHEN** the treemap scenario runs alone in a fresh FastFiles lifecycle
- **THEN** it passes on repeated runs without internal retry-until-pass
  loops, and the historical SKIPPED reason `treemap-drawn-on-d2d-surface`
  no longer appears
