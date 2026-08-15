# Design — Semantic UIA for Core Navigation/Search Surfaces

## Scope

Real Microsoft UI Automation providers for (1) Column View, (2) Home / Quick
Access as rendered inside Column View, and (3) the Search result list. Treemap
is intentionally out of scope (follow-up); the architecture leaves room for a
future Treemap fragment without rework.

## D1. Why the surfaces were invisible (root cause)

FastFiles renders Column View, Home, and Storage Analysis content into a
single DirectComposition-backed HWND (`FastFilesMainWindow`). The window
procedure handles no `WM_GETOBJECT` at all (verified by repository-wide
search), so UIAutomationCore falls back to the default HWND provider: the
window appears as a plain Pane/Window whose children are only the native
Win32 child controls (chrome, palette, search controls). Everything painted
by Direct2D has no element and no properties. The search result list is a
native `SysListView32` with `LVS_OWNERDATA | LVS_OWNERDRAWFIXED | LVS_REPORT`
(header hidden); the UIA/MSAA proxy cannot enumerate virtual owner-data
list children, so result rows never appear either. (Live observation of the
native bridge with populated results was not possible this session — the
host's engine cannot publish an initial snapshot generation (pre-existing,
reproduced on the baseline); the custom provider is validated headlessly and
structurally live — see `evidence/e2e-validation.md`.)

## D2. Integration: WM_GETOBJECT on the main HWND

`WindowShell::HandleMessage` handles `WM_GETOBJECT` with
`lParam == UiaRootObjectId` and returns
`UiaReturnRawElementProvider(hwnd, wParam, lParam, rootProvider)`.

- The root provider is a fragment root (`IRawElementProviderFragmentRoot`)
  with `ControlType` **Window**, `Name` = live window text (at launch
  "FastFiles"), `AutomationId` = `MainWindow`, and
  `HostRawElementProvider` from `UiaHostProviderFromHwnd`. Returning a
  Window control type preserves the element identity the existing driver
  relies on (scenario 3.1 finds `FastFilesMainWindow` and asserts the title).
- Native child HWNDs (navigation chrome, sidebar, command palette, search
  panel controls, dialogs) keep their own providers. The UIA framework
  includes child windows of an HWND-based fragment root automatically; this
  is verified in the live E2E (chrome elements must remain discoverable). If
  the host's behavior ever differs, the root's child list additionally
  exposes the native children via `UiaHostProviderFromHwnd` (documented
  fallback, not implemented speculatively).
- The app keeps one own COM reference to the root provider for the lifetime
  of the window; `WM_DESTROY` releases it. UIAutomationCore holds its own
  references on behalf of clients, so a client may outlive the window — the
  provider becomes a safe zombie (D6).

## D3. Provider ownership and the pure projection seam

New files (all in the `FastFiles` target; providers must not bury COM
plumbing in `ColumnView.cpp`):

- `src/ui/include/ffui/UiaSurface.h` — the pure seam. An abstract
  `UiaSurface` interface with read-only queries (`PaneInfo`, `ColumnInfo`,
  `ItemInfo`, `HomeInfo`, `SearchInfo`, geometry) and action methods
  (`ActivateItem`, `SelectItem`, `ToggleItem`, `RangeSelectItem`,
  `SetItemFocus`, `ActivateHomeItem`, `ActivateHomeClear`, `SetHomeFocus`,
  `ScrollPaneBy`, `ScrollItemIntoView`, `SelectSearchItem`,
  `ActivateSearchResult`, ...). Returned data structs carry exactly the
  fields the providers need; nothing else. The providers call **only** this
  interface — never `ColumnView`/`SearchPanel` directly.
- `src/ui/src/UiaProviderBase.{h,cpp}` — `UiaProviderBase` implementing
  `IUnknown` (refcount + QueryInterface table), VARIANT helpers, common
  property retrieval, runtime-ID helpers, screen-coordinate conversion,
  event raising helpers, and the owner-lifetime token.
- `src/ui/src/ColumnViewUiaProvider.{h,cpp}` — root, Column View group,
  pane, column, and column-item providers.
- `src/ui/src/HomeUiaProvider.{h,cpp}` — Home group, section, Home item, and
  Clear/quick-action button providers (fragments under a Home pane).
- `src/ui/src/SearchUiaProvider.{h,cpp}` — search list fragment root and
  result item providers, hosted on the result `ListView` HWND.

`ColumnView` implements `UiaSurface` in `ColumnView.cpp` (thin forwarding
under `columnsMutex_`); `SearchPanel` implements the search-facing part. This
keeps provider logic 100% headless-testable with a fake `UiaSurface` — the
established FastFiles pattern (`HomeLayout.h`, `SelectionIdentity.h`,
`ShellLayout.h`) — and guarantees the providers project the exact real state
(no parallel accessibility-only state model). Mutations performed through
`UiaSurface` action methods call the exact methods real input calls
(`ActivateItem`, `ApplySelectionClick` via `SelectSingle`/`ToggleSelection`/
`SelectRange`, the `HomeActivation` callback, the search `navigate_`
callback, the real scroll clamp math).

## D4. Provider hierarchy

```
FastFiles window (fragment root, Window, "FastFiles", MainWindow)
 ├─ [native child HWND elements — chrome, sidebar, palette, ...]
 └─ Column View (Group, "Column View", AutomationId "ColumnView")
     ├─ Pane.0 (Pane, AutomationId "Pane.0", Scroll pattern)
     │   ├─ Pane.0.Column.0 (List, AutomationId "Pane.0.Column.0", Selection pattern)
     │   │   ├─ item (ListItem, SelectionItem+Invoke+ScrollItem)
     │   │   └─ ...
     │   └─ Pane.0.Column.1 ...
     └─ Pane.1 (only when dual-pane is active)
         └─ ... or, in Home mode:
     ├─ Pane.N (Pane)
     │   └─ Home (Group, "Home", "Pane.N.Home")
     │       ├─ Pinned (Group, "Pinned", "Pane.N.Home.Section.Pinned")
     │       │   ├─ item (ListItem, Invoke)
     │       │   └─ ...
     │       ├─ Recent folders (Group; Clear button: Button "Clear recent folders")
     │       ├─ Recent searches (Group; Clear button)
     │       ├─ Common locations (Group; unavailable folder items: IsEnabled=false, no Invoke)
     │       └─ Quick actions (Group; Button+Invoke items)
```

Search: the result `ListView` HWND hosts its own fragment root
(`Search.Results`); its children are result ListItems. The tree appears
under the main window wherever the native list element sits today.

`IRawElementProviderFragment::Navigate` implements Parent/NextSibling/
PreviousSibling/FirstChild/LastChild for every provider; the fragment root
implements `ElementProviderFromPoint` (deepest element under a client point;
null outside the navigation viewport) and `GetFocus` (the current logical
focus target, or null when focus belongs to a native control).

## D5. Runtime IDs and stale-provider safety

Runtime IDs are arrays of ints with a leading namespace discriminant so
surfaces can never collide:

| Element | Runtime ID |
|---|---|
| Root | `{0,0,0,0}` |
| Pane.N | `{0,1,N,0}` |
| Column (pane N, index C) | `{0,2,N,pathHashLo,pathHashHi}` |
| Column item | `{0,3,N,pathHashLo,pathHashHi,idHashLo,idHashHi}` |
| Home group / section / clear / item | `{1,N,kind,section[,index,valHashLo,valHashHi]}` |
| Search list / result | `{2,0,0,0}` / `{2,1,idHashLo,idHashHi}` |

- **Column identity** = the column's full path (normalized; the "This PC"
  synthetic root uses a fixed sentinel). A column keeps its runtime ID across
  snapshot refreshes; navigation replaces the columns vector, so a stale
  column element's path hash no longer resolves and lookup fails cleanly.
- **Item identity** = the strongest identity in the real model: indexed
  `volumeRowId + fileIdHigh + fileIdLow` when present, else the case-folded
  full path. Refresh keeps the same item element valid; a *different* item
  that later occupies the same row index has a different hash, so a stale
  element can never retarget it.
- **Home items** = `section + indexInSection + value hash` (value = path /
  query / command id). A Home data refresh that replaces an entry at the
  same index changes the hash → stale element fails.
- **Search results** = `volumeId + id` when indexed, else
  `folder + name` hash. A new query replaces the result set; stale elements
  fail instead of retargeting.

Resolution is always by runtime-ID key against the **current** model, never
by row index. Unresolvable elements return `UIA_E_ELEMENTNOTAVAILABLE` from
property getters and null from `Navigate`.

## D6. Lifetime and threading

- Every provider holds a `std::shared_ptr<UiaOwnerState>`
  (`std::atomic<bool> alive`) plus a raw `UiaSurface*`. The owner
  (`ColumnView`/`SearchPanel`) constructs the state with the provider and
  sets `alive = false` in its destructor *before* member destruction. A
  provider whose owner is dead returns `UIA_E_ELEMENTNOTAVAILABLE` for every
  property and never dereferences the owner.
- Providers are owned by their surface (`ColumnView` owns
  `ColumnViewUiaProvider` lazily on first `WM_GETOBJECT`; `SearchPanel`
  similarly). The app releases its own references on `WM_DESTROY`;
  UIAutomationCore keeps client references alive, leaving safe zombies.
- Thread affinity: `WM_GETOBJECT` and all provider calls arrive on the UI
  thread (UIAutomationCore marshals through the HWND's provider thread).
  Property getters take `columnsMutex_` where the underlying accessor does
  (cheap, non-blocking, no I/O), never block on search/preview/engine
  threads, and never call `SendMessage`. Screen conversion uses
  `ClientToScreen` (no message traffic). Events are raised from UI-thread
  mutation sites only.
- Reentrancy: `UiaSurface` action methods and event notifications are
  invoked **after** the view releases its mutex, so a UIA client callback
  (which may re-enter the provider) can never deadlock on a held lock.

## D7. Focus semantics

The logical focus is projected, not mirrored:

- Column item: `HasKeyboardFocus` iff the main window owns real focus
  (`GetFocus() == hwnd`), the pane is active, the column is the pane's
  focused column, and the item is the column's `focusIndex`.
- `SetFocus` on an item sets the pane's focused column and the column's
  `focusIndex` only — selection is untouched (the real model already permits
  focus ≠ selection, e.g. Ctrl+click). Implemented as a new thin
  `ColumnView::SetItemFocus(pane, column, row)` so the provider never
  mutates state directly.
- Home: `HasKeyboardFocus`/`SetFocus` map to the typed `HomeFocusEntry`
  (`Item` or a Clear stop). `SetFocus` on a disabled item is rejected.
- Search: focus maps to the native list's `LVIS_FOCUSED`/selection state
  plus `GetFocus() == list_`; `SetFocus` selects+focuses the row and moves
  real focus to the list.
- `GetFocus` on the fragment root returns the current logical focus target
  provider (or null → runtime falls back to native focus).

## D8. Selection semantics

- Column elements implement `ISelectionProvider`: `CanSelectMultiple = true`
  (the real model supports Ctrl/Shift multi-select), `IsSelectionRequired =
  false`, `GetSelection` = providers for `selectedIndices`.
- Items implement `ISelectionItemProvider` reading `selectedIndices`:
  `Select` → `SelectSingle`, `AddToSelection`/`RemoveFromSelection` →
  `ToggleSelection` gated on the current state (only toggles when the
  operation actually changes membership).
- Search list implements `ISelectionProvider` with
  `CanSelectMultiple = false` (native `LVS_SINGLESEL`); result items
  implement `ISelectionItemProvider` over the list's real selection.
- All mutations call the real selection functions (`ApplySelectionClick`
  family / `ListView_SetItemState`), so UIA-driven selection is
  indistinguishable from mouse/keyboard selection.

## D9. Scrolling semantics

- Pane elements implement `IScrollProvider` over the real per-pane offsets:
  vertical = `verticalScrollOffset_` / `verticalScrollOffset2_` clamped
  exactly like `ColumnView::ScrollVertical` (focused column's item count ×
  `kRowHeight` vs. viewport); horizontal = `scrollOffset_` /
  `scrollOffset2_` over `contentWidth - paneWidth`. Home panes are
  vertically scrollable against `HomeLayoutResult::contentHeight` and not
  horizontally scrollable. `ScrollPercent`/`ViewSize` derive from the same
  clamp; zero max → 0% and not scrollable.
- `ScrollIntoView` (ScrollItem pattern on items and Home items) calls a
  `ScrollItemIntoView` action that reuses the exact clamp math of
  `EnsureFocusedItemVisible` without moving selection or focus.
- No fake accessibility-only scroll position: percent values are computed
  from the real offsets used by rendering and input.

## D10. Invoke semantics

- Column items: `Invoke` → `ActivateItem(column, row, false, false)` — the
  same path Enter and single-click use (folders populate the next column;
  files select). Double-click's shell-association open is intentionally not
  bound to `Invoke` (matching Enter, and avoiding launching applications
  from automation).
- Home items: `Invoke` → `ComputeHomeActivation` + the existing
  `homeActivateCallback_` (Folder → navigate, Search → execute query,
  QuickAction → command) — the identical route keyboard and mouse use.
  Clear affordances → `HomeActionKindForFocusTarget` activation through the
  same callback. Disabled items expose no Invoke pattern.
- Search results: `Invoke` → the same candidate→`ReconstructPath`→
  `navigate_` path as `NM_DBLCLK`/`LVN_ITEMACTIVATE`.

## D11. Bounding rectangles, offscreen, DPI

- All geometry is derived from the real render math (`x = paneX +
  i*kColumnWidth - hScroll`; `y = contentTop + r*kRowHeight - vScroll`,
  `contentTop = kBadgeHeight [+24 when a loading status line is drawn]`;
  Home item rects from `ComputeHomeLayout`; Clear rects from
  `homeClearButtons_`; search rows from `ListView_GetItemRect`).
- The shell pushes a small geometry struct into the provider controller on
  layout/DPI changes and every render (cheap floats): navigation-viewport
  origin in client DIPs, viewport size in DIPs, `dpiScale`, and the HWND.
- `BoundingRectangle` = DIP rect × `dpiScale` → client pixels →
  `ClientToScreen` (physical screen pixels, the UIA contract). Never a
  96-DPI assumption; PerMonitorV2 `WM_DPICHANGED` updates the struct.
- `IsOffscreen` = the element's rect has no intersection with the visible
  viewport clip (badge band excluded); partially visible elements are not
  offscreen. Home items respect the pane clip in dual-pane mode.
- High Contrast/theme: providers expose state, not pixels — no theme
  dependency. Names come from the data model, never colors.

## D12. Events

The views notify the provider controller through a
`std::function<void(const UiaEventBatch&)>` sink set by `WindowShell`
(`ColumnView::SetUiaEventSink`, `SearchPanel::SetUiaEventSink`), called
**after** mutation and lock release. The controller resolves targets by
runtime ID and raises:

- `AutomationFocusChangedEventId` — keyboard focus moves (arrows, Home/End,
  Enter, mouse activation, UIA SetFocus, pane switch, focus drop after Home
  refresh).
- `SelectionPattern_InvalidatedEventId` on the owning column / search list,
  plus `SelectionItemPattern_ElementAddedToSelectionEventId` /
  `ElementRemovedFromSelectionEventId` on the affected items, whenever
  selection membership changes.
- `StructureChangedEventId` — navigation (columns replaced), Home data
  refresh, search result set replacement, dual-pane enable/disable, pane
  Home mode switch. Bulk removal/addition variants where the API allows.
- `Invoke_InvokedEventId` after provider-driven activation.
- `AutomationPropertyChangedEventId` for `IsEnabled` transitions (Home
  unavailable locations) and Name changes where rename is observable
  (reported via the structure change of a refresh — never on a render tick).

Events are semantic-only: no structure event on paint, no per-frame work,
and raising is cheap when no client is attached (UIAutomationCore no-ops).

## D13. Virtualization and performance bounds

- Column items: one lightweight provider per queried item, created on
  demand by the controller, cached per column with a hard cap (overflow
  clears the column's cache — deterministic and bounded). Structure change
  drops all item providers for the affected column. No client attached ⇒
  zero provider allocations; paint never touches the provider layer.
- Columns implement `IItemContainerProvider` (`FindItemByProperty` for
  Name/automation id) so clients can locate items without enumerating a huge
  directory.
- Search results: same on-demand model over `results_` (≤ `kMaxUiSearchResults`
  = 10 000 by the existing UI bound); the native ListView continues to own
  input, painting, and scrolling. No per-result HWND, no D2D objects, no
  layout pass per property query, no synchronous filesystem access.
- The synthetic large-directory benchmark (tests) enumerates a 10 000-item
  provider tree, records enumeration time, retained provider count, and
  cache size, and asserts the bounded-cache contract.
- No `O(total-index-size)` provider tree: columns bound the item surface to
  the current directory; search bounds to the result cap.

## D14. Dual-pane behavior

- Runtime IDs embed the pane index, so "same folder open in both panes" and
  "Home in one pane, filesystem in the other" can never collide.
- Pane 1 exists only while `dualPane_`; enabling/disabling raises structure
  events on the Column View group.
- `ActivatePane` moves logical focus and raises focus events; automation
  selection/invoke actions target the pane encoded in the element's runtime
  ID and can never act on the same-index item in the wrong pane.
- A stale pane-0 element stays bound to pane 0's model even after dual-pane
  toggles.

## D15. Search-results behavior

A custom fragment root replaces the list's UIA semantics (see D1 for why the
native proxy cannot expose virtual items). The subclassed window procedure
intercepts only `WM_GETOBJECT` with `lParam == UiaRootObjectId`; every other
message (including `OBJID_CLIENT`) falls through to `DefSubclassProc`, so
MSAA and native behavior are untouched. Exposed per result: Name (filename),
HelpText (containing folder), FullDescription (type, logical size, modified
time — logical size only; the search model has no allocated size and the
data pipeline is **not** widened for accessibility), ControlType ListItem,
selection/focus state, BoundingRectangle, IsOffscreen, SelectionItem +
Invoke. Result-set replacement raises a structure-changed event.

## D16. Security boundaries

Providers expose only state already displayed to the interactive user:
names, paths, sizes, times, selection/focus/scroll. No file content, no
service internals, no raw MFT bytes, no signing information, no diagnostic
paths the UI does not show. No change to the privileged service, engine, or
protocol is required; if any implementation step requires one, the change
stops and the architecture is re-evaluated.

## D17. Validation and closure of prior SKIPs

Provider tests (headless, fake `UiaSurface`): hierarchy/names/control types,
runtime-ID stability, focus, single/multi selection, ScrollIntoView,
offscreen, dual panes, same-path-both-panes, Home sections/disabled
folders/Clear buttons/actions/focus parity, search result metadata/
selection/activation/virtualization, stale providers for all three surfaces,
COM lifetime (QI/refcount/owner destruction/repeated create-destroy),
geometry conversion, and events. The synthetic large-directory benchmark
feeds the performance gate.

The live E2E (`windows-ui-automation-validation` capability + uia-driver)
re-runs the previously SKIPPED scenarios:

- 3.2 `column-populated-and-selected-item-in-tree` — new provider makes
  selected column items discoverable.
- 3.3 `selection-and-scroll-patterns` — pane Scroll + column Selection
  patterns replace `no-scroll-selection-provider`.
- 3.5 `in-column-error-state` — the unavailable column exposes its error
  text as an element (accessibility name on the column/status element).
- 3.7/3.8/3.9 drag scenarios — deterministic source item `BoundingRectangle`
  + driver pointer drag (Down/move/Up via SendInput), filesystem state
  verified programmatically.
- 3.11 `search-deep-result-navigation` / `search-structured-filter-query` —
  results discoverable, activation verified via real navigation state.

Remaining SKIPs stay only where an independent blocker exists (privileged
scan unavailable, service not running).
