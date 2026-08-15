# Tasks — semantic-uia-core-surfaces

Traceability: code comments cite tasks as `// semantic-uia-core-surfaces N.M:`.

## 1. Foundation

- [x] 1.1 `src/ui/src/UiaProviderBase.{h,cpp}` — `UiaProviderBase` (IUnknown
  refcount + QueryInterface table), VARIANT helpers, common
  `IRawElementProviderSimple` property retrieval, `UiaOwnerState`
  (shared lifetime token, `alive` flag), event-raising helpers, and
  DIP→physical→screen coordinate conversion helper.
- [x] 1.2 `src/ui/src/UiaRuntimeIds.h` — runtime-ID namespaces per surface,
  stable identity hashing (FRN/volumeRowId, path, Home value, search
  identity), and deterministic ID builders.
- [x] 1.3 `src/ui/include/ffui/UiaSurface.h` — pure projection seam:
  `UiaPaneInfo`/`UiaColumnInfo`/`UiaItemInfo`/`UiaHomeInfo`/
  `UiaHomeSectionInfo`/`UiaSearchInfo` structs + abstract `UiaSurface`
  read/action interface. Header-only, no renderer/engine dependencies.
- [x] 1.4 `src/ui/src/UiaViewGeometry.h` — geometry struct (viewport origin
  DIP, viewport size DIP, dpiScale, hwnd) shared by the providers.

## 2. Column View provider

- [x] 2.1 `ColumnViewUiaProvider.{h,cpp}` — fragment root (Window,
  `MainWindow`), Column View group (`ColumnView`), pane provider
  (`Pane.N`, Scroll pattern), column provider (`Pane.N.Column.M`, List,
  Selection pattern + ItemContainer), item provider (ListItem,
  SelectionItem + Invoke + ScrollItem).
- [x] 2.2 Runtime IDs per D5 (pane-scoped, path/identity-hashed; stale
  lookup fails cleanly).
- [x] 2.3 `ColumnView::SetItemFocus(pane, column, row)` focus-only setter;
  `ScrollItemIntoView(pane, column, row)` reusing the real clamp math.
- [x] 2.4 `ColumnView` implements `UiaSurface` (read queries under
  `columnsMutex_`, actions routed to real methods) + lazy provider
  ownership + `SetUiaEventSink`.
- [x] 2.5 WindowShell: `WM_GETOBJECT` integration on the main HWND,
  geometry push on layout/DPI/render, `WM_DESTROY` release.
- [x] 2.6 ColumnView event wiring: focus/selection/structure/invoke events
  raised post-lock on the mutations that own them.

## 3. Home provider

- [x] 3.1 `HomeUiaProvider.{h,cpp}` — Home group, section groups
  (non-selectable), item providers (ListItem + Invoke; disabled items
  `IsEnabled=false` and pattern-free), Clear affordance and Quick action
  Buttons (Invoke).
- [x] 3.2 Home runtime IDs (section + index + value hash) and stale
  refresh behavior.
- [x] 3.3 Home focus mapping (`HomeFocusEntry` ↔ HasKeyboardFocus/SetFocus)
  and activation via the existing `HomeActivation` callback only.
- [x] 3.4 `SetHomeData`/`RevalidateHomeFocus` structure/focus event wiring.

## 4. Search provider

- [x] 4.1 Root cause recorded: `LVS_OWNERDATA` virtual lists are not
  enumerated by the UIA/MSAA proxy (the bridge reports no list children for
  virtual owner-data lists; the list never materializes rows for the bridge
  to query). The custom fragment root replaces the list's UIA semantics;
  input/virtualization stay native. Live verification of the native bridge
  with data was not possible this session (pre-existing engine-connect host
  state); the custom provider is validated headlessly
  (`ffsearch_uia_tests`) and structurally live (`Search.Results` element in
  the tree). Evidence: `evidence/e2e-validation.md`.
- [x] 4.2 `SearchUiaProvider.{h,cpp}` — fragment root on the result list
  HWND (subclassed `WM_GETOBJECT`, everything else to
  `DefSubclassProc`), result item providers (Name/HelpText/
  FullDescription, SelectionItem, Invoke).
- [x] 4.3 Search selection/focus mapped to native `ListView` state;
  activation through the real `navigate_` path.
- [x] 4.4 Result-set replacement structure events; stale result safety;
  on-demand items (no per-result allocation until queried).

## 5. Driver + validation closure

- [x] 5.1 Extend `verify/uia-driver` with general ScrollItem and
  ItemContainer consumption (pattern resolution/invocation) and mock
  support; headless driver tests.
- [x] 5.2 Rework `windows-ui-automation-validation` capability:
  - 3.2 `column-populated-and-selected-item-in-tree` real assertion;
  - 3.3 `selection-and-scroll-patterns` real pattern asserts
    (multi-select via keyboard, selection via SelectionItem, scroll via
    Scroll pattern);
  - 3.5 `in-column-error-state` via the unavailable column element;
  - 3.7/3.8/3.9 drag via item `BoundingRectangle` + pointer drag +
    programmatic filesystem verification;
  - 3.11 search: execute a real query, assert result items, deep
    navigation via Invoke, structured-filter query.
- [x] 5.3 Accessibility-inspection evidence: live tree dump containing real
  dynamic items (files/folders in columns, Home sections, search results).

## 6. Tests

- [x] 6.1 `tests/ui/test_columnview_uia.cpp` — hierarchy, names, control
  types, runtime-ID stability, focus, selection, multi-selection,
  ScrollIntoView, offscreen, dual panes, same path both panes, stale item
  after navigation, COM lifetime (QI/AddRef/Release, owner destruction,
  repeated create/destroy, no leaked refs in the harness).
- [x] 6.2 `tests/ui/test_home_uia.cpp` — section/item hierarchy, disabled
  unavailable folder, Clear buttons, quick actions, focus parity, stale
  item after refresh.
- [x] 6.3 `tests/ui/test_search_uia.cpp` — result count, name/path
  metadata, selection, activation, virtualization, stale result after new
  query, several-thousand-result behavior.
- [x] 6.4 `tests/ui/test_uia_geometry.cpp` — client→screen conversion,
  scroll offsets, DPI transformation, offscreen math, no-hardcoded-96-DPI.
- [x] 6.5 `tests/ui/test_uia_events.cpp` — focus, selection, structure/
  result-change event descriptors and post-lock raising contract.
- [x] 6.6 `tests/ui/bench_uia_enumeration.cpp` — synthetic 10 000-item
  directory provider enumeration benchmark (time, retained provider count,
  cache size) + CTest registration; report numbers in the change evidence.

## 7. Docs

- [x] 7.1 Durable UIA provider conventions in AGENTS.md (only the
  cross-session rules: WM_GETOBJECT point, UiaSurface seam, runtime-ID
  namespaces, stale/lifetime contract).
- [x] 7.2 Zero-touch tasks.md: provider-absence SKIPs closed by this change
  no longer stated as current limitations (preserve historical entries).
- [x] 7.3 Canonical spec sync: new `ui-automation` capability + coordination
  notes in `column-view-browsing`, `quick-access-home`, `search-ui`.
- [x] 7.4 WORKSTATE checkpoint + validation record.
