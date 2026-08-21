# Audit area: UIA / ACCESSIBILITY SURFACE — audit src/ui/src/UiaProviderBase.{h,cpp}, ColumnViewUiaProvider.{h,cpp}, SearchUiaProvider.{h,cpp}, TreemapUiaProvider.{h,cpp}, UiaRuntimeIds.h, UiaGeometry.h, UiaNotifications.h, src/ui/include/ffui/UiaSurface.h, UiaEvents.h, and WM_GETOBJECT wiring (WindowShell/Main). Cover: pattern correctness (Invoke/SelectionItem/ScrollItem/ExpandCollapse/Value/LegacyIAccessible as applicable), stale-element self-verification and UIA_E_ELEMENTNOTAVAILABLE, event emission timing/ordering, bounding rectangles physical-pixel/DPI correctness, IsOffscreen logic. ALSO DELIVERABLE: map precisely how the main window's Win32 child controls (search sort ComboBox control id ~7103, sort button ~8202, edit boxes) are exposed today — known issue: custom provider reports them as ControlType=Pane, owner-drawn dropdown items not UIA-discoverable, ExpandCollapse on Pane does nothing. Document exactly where the fragment root handles child/navigate so a fix can expose correct ControlTypes and delegate patterns to native HWND proxies.

(agent agent-9, outcome completed)

## Defects

### [P1] Entire Win32 control surface invisible to UIA — `get_HostRawElementProvider` never returns the host provider — UiaProviderBase.cpp:21
- Symptom (user-visible): Narrator/Accessibility Insights/any screen reader sees only the custom Column View fragment. The search query combo (id 7101), scope combo (7102), sort combo (7103), "Clear history" button, sort-direction button (7107), navigation chrome buttons (e.g. forward, id 8202), and every other native child HWND of the main window are absent from the accessibility tree. Keyboard/UIA users cannot discover or operate them; this is the root cause of the known "reported as ControlType=Pane / ExpandCollapse does nothing" symptom class.
- Evidence: `UiaProviderBase::get_HostRawElementProvider` unconditionally does `*retVal = nullptr; return S_OK;` (UiaProviderBase.cpp:21–25). Both fragment roots *create* host providers (`UiaHostProviderFromHwnd` at ColumnViewUiaProvider.cpp:130 and SearchUiaProvider.cpp:120) but use them **only** as a property fallback inside `GetPropertyValue` (ColumnViewUiaProvider.cpp:209–211) — never from `get_HostRawElementProvider`. Grep confirms no override exists anywhere (`src/ui`, only the base hits).
- Failure scenario: client walks from the desktop → main window element is the custom root; `Navigate(FirstChild)` goes Group→Pane→Columns; there is no path to any child HWND, and because the root claims no host provider, UIA will not proxy-fallback the child windows either.
- Root cause hypothesis: the host provider was wired for property deferral only; the interface method that actually merges HWND backing was left at the base-class null default.
- Suggested fix (minimal): in `ColumnViewUiaProvider` and `SearchUiaProvider`, override `get_HostRawElementProvider` to return `hostProvider_` (AddRef'd). Then expose the native controls: either (a) set `UIA_NativeWindowHandlePropertyId` on the root and add fragment children that wrap each control HWND via `UiaHostProviderFromHwnd(child)` returned from `Navigate`, or (b) simplest correct shape — make the root's `Navigate(FirstChild/NextSibling)` chain yield wrapper fragments whose `get_HostRawElementProvider` is the child HWND's host provider, giving correct ControlTypes (ComboBox/Button/Edit) and native patterns (ExpandCollapse/Value/Invoke) for free.
- Regression test idea: headless fake-hwnd test asserting `get_HostRawElementProvider` on both roots returns non-null, plus a navigation walk enumerating N control-wrapper children matching the created control HWNDs.

### [P1] Owner-drawn ComboBoxes have no accessible content even after host-provider exposure — SearchPanel.cpp:214–217
- Symptom: scope/sort combos are `CBS_OWNERDRAWFIXED | CBS_HASSTRINGS`; the sort-direction button is `BS_OWNERDRAW`. Once the controls become visible to UIA (defect above), an owner-draw combo's proxied element exposes no item texts through native UIA (draw data lives in app code), and the owner-draw button reports no name/state beyond the window text ("Ascending" set once at creation, never updated on toggle — SearchPanel.cpp:224 vs. the direction flip handling around line 354).
- Evidence: SearchPanel.cpp:214 (`scope_`), 216 (`sort_`), 224 (`sortDirection_` with `BS_OWNERDRAW`); strings added only via `CB_ADDSTRING` (240–241) which owner-draw combos do not render from, and DrawItem code supplies visuals only.
- Failure scenario: assistive-tech user opens the sort dropdown; items "Relevance/Filename/Path/…" are not individually discoverable or selectable by name.
- Root cause: owner-draw removes the system's text rendering that the default UIA proxy relies on.
- Fix: either drop owner-draw for these two combos (they're themed via `SetDarkTheme`-adjacent code anyway — cost: lose dark styling) or implement small fragment providers for the three controls (ComboBox: ExpandCollapse + Selection + SelectionItem list; button: Toggle/Invoke + Name updated on state change). Do not leave them as bare Panes.
- Test idea: unit test over the new provider asserting each combo item's Name equals the string passed to `CB_ADDSTRING` and ExpandCollapse patterns exist.

### [P2] Search-list hit-test uses the wrong word of `ListView_GetItemSpacing` — SearchUiaProvider.cpp:250–253
- Symptom: `ElementProviderFromPoint` on the search results resolves the wrong row, or no row at all.
- Evidence:
```cpp
const LONG spacing = ListView_GetItemSpacing(hwnd, TRUE);
const LONG rowHeight = spacing > 0 ? LOWORD(static_cast<DWORD>(spacing)) : 28;
```
Per `LVM_GETITEMSPACING`, **HIWORD** is vertical spacing (row height); LOWORD is horizontal. In `LVS_REPORT` (the list's mode, SearchPanel.cpp:219) LOWORD is typically the full item width or 0 — if 0, the `rowHeight <= 0` guard silently returns null for every point query.
- Failure scenario: Accessibility Insights "hover inspect" or any UIA client hit-testing the list gets a different element than the row under the cursor.
- Fix: `const LONG rowHeight = HIWORD(static_cast<DWORD>(spacing));` keep the fallback.
- Test idea: headless test feeding synthetic spacing values `(width<<16)|height` and asserting row selection uses height.

### [P2] Fragment-root group's `Navigate(LastChild)` always returns Pane 1 — ColumnViewUiaProvider.cpp:950–957
- Symptom: in dual-pane mode, TreeWalker last-child traversal from "Column View" lands on Pane 1; Pane 2 is reachable only via NextSibling, breaking depth-first traversal contracts some ATs rely on (and any client caching "last child").
- Evidence:
```cpp
case NavigateDirection_FirstChild:
case NavigateDirection_LastChild: {
    auto pane = owner_->PaneElement(0);
```
Both directions return `PaneElement(0)` unconditionally, even though `PaneUiaProvider::Navigate(NextSibling)` exposes pane 1 (line 1089–1096).
- Fix: for `LastChild`, return `PaneElement(surface->UiaPaneCount() - 1)`.
- Test idea: dual-pane fake surface; assert FirstChild==pane0, LastChild==pane1, single-pane LastChild==pane0.

### [P2] Blind `static_cast` of foreign providers in `FindItemByProperty` startAfter resolution — ColumnViewUiaProvider.cpp:109–118, SearchUiaProvider.cpp:100–109, TreemapUiaProvider.cpp:222–231
- Symptom: undefined behavior (garbage `Pane()/Column()/Row()/Index()` reads) when a UIA client passes a sibling from a *different* container as `startAfter` — legal per the ItemContainer contract, and plausible with cross-container automation scripts since all three surfaces' items implement `ISelectionItemProvider`.
- Evidence: `auto* item = static_cast<ColumnItemUiaProvider*>(selection.Get());` then non-virtual inline accessors read fields at wrong offsets. Same pattern in `IndexOfResultProvider` and the treemap's fragment cast.
- Fix: verify identity before casting — QI for `IRawElementProviderFragment`, compare `GetRuntimeId()` namespace discriminant (`kUiaNsColumnView` kind==3 / `kUiaNsSearch` kind==1 / treemap kinds) against this container, treat mismatch as "start from beginning".
- Test idea: pass a search-result provider as `startAfter` to a column's `FindItemByProperty`; assert it starts from row 0 rather than crashing.

### [P3] `std::wstring(nullptr, 0)` UB in treemap AutomationId match — TreemapUiaProvider.cpp:240–241
- Evidence: `TileAutomationId(...) == std::wstring(value.bstrVal, value.vt == VT_BSTR ? SysStringLen(value.bstrVal) : 0)` — when `value.vt != VT_BSTR`, `value.bstrVal` is still read (union type-pun) and may be a garbage non-null pointer; `std::wstring(ptr, 0)` with invalid ptr is UB.
- Fix: early-out `if (value.vt != VT_BSTR || !value.bstrVal) return S_OK;` before the loop's AutomationId branch.

### [P3] Column status text always reports `IsOffscreen=false` — ColumnViewUiaProvider.cpp:1975–1977
- Evidence: `case UIA_IsOffscreenPropertyId: SetVariantBool(retVal, false);` while its `BoundingRectangle` (1978–1988) is computed from live hScroll and can be scrolled entirely out of the pane clip. Inconsistent bounds/offscreen pairs confuse AT geometry consumers. Fix: compute `UiaRectOffscreen(rectDip, paneX, paneWidth, heightDip)` like every other element.

### [P3] Pane elements hardcode `HasKeyboardFocus=false` — ColumnViewUiaProvider.cpp:1046–1048
- Evidence: `SetVariantBool(retVal, false);` unconditionally, although `PaneUiaProvider::SetFocus` (1157) activates the pane and the pane advertises `IsKeyboardFocusable=true`. After a UIA focus call lands on the pane (no focused row/column case), clients reading HasKeyboardFocus get false while the window really has focus. Fix: project `surface->UiaActivePane() == pane_ && geometry.keyboardFocus && focusedRow < 0`.

### [P3] SearchListProc swallows WM_GETOBJECT when the provider can't be built — SearchPanel.cpp:165–174
- Evidence: if `UiaRootProvider` returns null (surface dead), the handler `return 0;` ("unobtainable") instead of falling through to `DefSubclassProc`, killing the native ListView MSAA/UIA fallback precisely during teardown when it would help exiting AT clients. Low impact given the zombie-token design, but the fall-through is strictly safer. Same shape in WindowShell.cpp:1629.

## Deliverable: how the main window's Win32 child controls are exposed today

**Creation** (all children of the main HWND, `owner`): SearchPanel.cpp:212–225 — query combo `COMBOBOX` id **7101**, scope combo id **7102** (owner-draw), sort combo id **7103** (owner-draw), ListView id ~7104 area (`kListId`, LVS_REPORT|OWNERDATA|SINGLESEL), status STATIC (no id), "Clear history" BUTTON, sort-direction BUTTON id **7107** (BS_OWNERDRAW). Navigation chrome buttons (e.g. forward **8202**) live under NavigationChrome (NavigationChrome.cpp:12).

**WM_GETOBJECT wiring**: main window — WindowShell.cpp:1619–1632 intercepts `lParam == UiaRootObjectId` and returns `columnView_.UiaRootProvider(hwnd)` via `UiaReturnRawElementProvider`; everything else → `DefWindowProcW`. Search list HWND — subclassed (`SearchListProc`, SearchPanel.cpp:162–177, installed at :238) answering `UiaRootObjectId` with `SearchUiaProvider`. Edit box (`queryEdit_`) and status have subclasses for input only, not UIA.

**Fragment child/navigate map** (the only structure UIA can see):
- Root `ColumnViewUiaProvider::Navigate` FirstChild/LastChild → Group or TreemapGroup (ColumnViewUiaProvider.cpp:219–235)
- Group → **always Pane 0** (:950–957)
- Pane → Home group or first/last Column (:1107–1124); siblings chain panes/columns
- Column → Status (if statusText non-empty) else item 0 / last item (:1485–1514)
- Item → parent column, row±1 (:1757–1792)

**Why controls show up wrong**: the roots create `UiaHostProviderFromHwnd` providers but never return them from `get_HostRawElementProvider` (see P1 #1), so no HWND-backed child is ever merged or enumerated; nothing in any `Navigate` implementation references a control HWND; no `UIA_NativeWindowHandlePropertyId` is set. Net result: the controls are simply absent (clients that force-inspect a child HWND directly get the default proxy, which for these owner-draw/custom-window-class surfaces degrades to generic Pane with no patterns — hence "ExpandCollapse on Pane does nothing"). A fix belongs at exactly two seams: (1) `get_HostRawElementProvider` overrides in `ColumnViewUiaProvider`/`SearchUiaProvider`, and (2) a Navigate-level bridge exposing per-control wrapper fragments backed by each child HWND's host provider (plus real providers or non-owner-draw for the two combos and the owner-draw button).

## Verified sound

- Stale-element discipline: `ColumnItemUiaProvider::Resolve` re-verifies pathHash + row range + full identity hash (never retargets same-index occupant) — ColumnViewUiaProvider.cpp:1647–1667; Home items verify section+index+valueHash (:2452–2463); search results verify idHash (SearchUiaProvider.cpp:433–443); treemap tiles verify scopeHash (TreemapUiaProvider.cpp:265–273). All fail with `UIA_E_ELEMENTNOTAVAILABLE`.
- Lifetime token: `alive` flipped before provider release in both `ColumnView::ReleaseUiaProvider` (ColumnView.cpp:2414–2433) and `~SearchPanel` (SearchPanel.cpp:180–200); treemap sink detached on UI thread before drop.
- Geometry/DPI: DIP→physical conversion consistent across all elements (`UiaRectToScreen` with dpiScale + ClientToScreen, null-hwnd identity for tests); item/column/home rects replicate render math; partial visibility correctly NOT offscreen (`UiaRectOffscreen`).
- Scroll pattern math: percent/view-size/scrollable getters mirror the real clamps including Home mode; `SetScrollPercent` validates range with `E_INVALIDARG`.
- Event ordering: notifications raised only after mutations release `columnsMutex_` (NotifyUiaSelectionTransition captures before/after snapshots, ColumnView.cpp:2459–2478); selection add/remove events derived from actual diff, not intent.
- Bounded caches with deterministic whole-clear overflow (items 4096, home 512, treemap tiles 1024, search results) and correct size-counter maintenance.
- `MakeUnknownArray` ref-counting correct (explicit AddRef before PutElement; SafeArrayDestroy releases).
- Honest degraded states: disabled Home items expose no Invoke and `IsEnabled=false`; treemap reports null focus and non-focusable tiles rather than faking it; status text becomes ELEMENTNOTAVAILABLE once no longer drawn.
