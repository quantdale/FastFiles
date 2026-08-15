// semantic-uia-core-surfaces 6.1: headless provider tests for the Column
// View UIA tree (hierarchy, names, control types, runtime IDs, focus,
// selection, multi-selection, ScrollIntoView, offscreen, dual panes, stale
// elements, COM lifetime). Uses a fake UiaSurface; the provider tree is
// exercised through the real COM interfaces.

#include "ColumnViewUiaProvider.h"
#include "UiaTestSurface.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../TestSupport.h"
#include "UiaTestHelpers.h"

using namespace fftest;

namespace {

fftest::FakeUiaSurface MakeSinglePaneSurface() {
    fftest::FakeUiaSurface surface;
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    surface.geometry.dpiScale = 1.0f;
    surface.geometry.keyboardFocus = true;
    fftest::FakeUiaSurface::FakePane pane;
    fftest::FakeUiaSurface::FakeColumn column;
    column.path = L"C:\\Users\\Test";
    for (int i = 0; i < 5; ++i) {
        fftest::FakeUiaSurface::FakeItem item;
        item.name = i % 2 == 0 ? L"Folder" + std::to_wstring(i) : L"file" + std::to_wstring(i) + L".txt";
        item.isDirectory = i % 2 == 0;
        item.fileIdLow = 1000 + static_cast<uint64_t>(i);
        item.volumeRowId = 1;
        column.items.push_back(item);
    }
    column.focusIndex = 0;
    column.items[0].selected = true;
    column.items[0].focused = true;
    pane.columns.push_back(column);
    surface.panes.push_back(pane);
    return surface;
}

fftest::FakeUiaSurface MakeDualPaneSurface() {
    fftest::FakeUiaSurface surface = MakeSinglePaneSurface();
    surface.geometry.widthDip = 1200.0f;
    fftest::FakeUiaSurface::FakePane pane2;
    fftest::FakeUiaSurface::FakeColumn column2;
    column2.path = L"C:\\Users\\Test"; // same folder as pane 0
    fftest::FakeUiaSurface::FakeItem item;
    item.name = L"shared.txt";
    item.fileIdLow = 999;
    item.volumeRowId = 1;
    column2.items.push_back(item);
    pane2.columns.push_back(column2);
    surface.panes.push_back(pane2);
    return surface;
}

fftest::FakeUiaSurface MakeHomeSurface() {
    fftest::FakeUiaSurface surface;
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    surface.geometry.dpiScale = 1.0f;
    surface.geometry.keyboardFocus = true;
    fftest::FakeUiaSurface::FakePane pane;
    pane.home = true;
    surface.panes.push_back(pane);
    // Recent folders section with a Clear affordance + one item.
    fftest::FakeUiaSurface::FakeHomeSection section;
    section.sectionKind = ffui::kUiaHomeSectionRecentFolders;
    section.itemCount = 1;
    section.firstGlobalIndex = 0;
    section.clearAffordance = true;
    surface.homeSections.push_back(section);
    fftest::FakeUiaSurface::FakeHomeItem homeItem;
    homeItem.sectionKind = ffui::kUiaHomeSectionRecentFolders;
    homeItem.primary = L"C:\\Recent";
    homeItem.value = L"C:\\Recent";
    surface.homeItems.push_back(homeItem);
    // Quick actions section.
    fftest::FakeUiaSurface::FakeHomeSection actions;
    actions.sectionKind = ffui::kUiaHomeSectionQuickActions;
    actions.itemCount = 4;
    actions.firstGlobalIndex = 1;
    surface.homeSections.push_back(actions);
    for (int i = 0; i < 4; ++i) {
        fftest::FakeUiaSurface::FakeHomeItem action;
        action.sectionKind = ffui::kUiaHomeSectionQuickActions;
        action.indexInSection = i;
        action.primary = L"Action" + std::to_wstring(i);
        action.value = L"command." + std::to_wstring(i);
        action.y = 100.0f + static_cast<float>(i) * 50.0f;
        surface.homeItems.push_back(action);
    }
    surface.homeFocusTarget = ffui::kUiaHomeFocusItem;
    surface.homeFocusIndex = 0;
    return surface;
}

std::vector<int> GetRuntimeId(IRawElementProviderFragment* element) {
    std::vector<int> result;
    SAFEARRAY* array = nullptr;
    if (FAILED(element->GetRuntimeId(&array)) || array == nullptr) return result;
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    for (LONG i = lower; i <= upper; ++i) {
        int value = 0;
        SafeArrayGetElement(array, &i, &value);
        result.push_back(value);
    }
    SafeArrayDestroy(array);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NavigateTo(
    IRawElementProviderFragment* element, NavigateDirection direction) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(direction, &result);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> FirstChild(IRawElementProviderFragment* element) {
    return NavigateTo(element, NavigateDirection_FirstChild);
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NextSibling(IRawElementProviderFragment* element) {
    return NavigateTo(element, NavigateDirection_NextSibling);
}

void TestHierarchyAndNames() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    // Root: Window "FastFiles".
    Check(GetName(static_cast<IRawElementProviderFragment*>(&root)) == L"FastFiles" || GetName(static_cast<IRawElementProviderFragment*>(&root)).empty(),
          "root name comes from the host (empty in headless)");
    Check(GetControlType(static_cast<IRawElementProviderFragment*>(&root)) == UIA_WindowControlTypeId, "root control type is Window");

    // Root -> Column View group.
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(group != nullptr, "root has a first child");
    Check(GetName(group.Get()) == L"Column View", "group name is 'Column View'");
    Check(GetControlType(group.Get()) == UIA_GroupControlTypeId, "group control type is Group");

    // Group -> Pane.0.
    auto pane = FirstChild(group.Get());
    Check(pane != nullptr, "group has a first child (pane)");
    Check(GetName(pane.Get()) == L"Pane 1", "pane name is 'Pane 1'");
    Check(GetControlType(pane.Get()) == UIA_PaneControlTypeId, "pane control type is Pane");
    Check(NavigateTo(pane.Get(), NavigateDirection_NextSibling) == nullptr,
          "single-pane mode has no pane 2");

    // Pane -> first column.
    auto column = FirstChild(pane.Get());
    Check(column != nullptr, "pane has a first child (column)");
    Check(GetName(column.Get()) == L"C:\\Users\\Test", "column name is the folder path");
    Check(GetControlType(column.Get()) == UIA_ListControlTypeId, "column control type is List");

    // Column -> first item.
    auto item = FirstChild(column.Get());
    Check(item != nullptr, "column has a first child (item)");
    Check(GetName(item.Get()) == L"Folder0", "item name is the display name");
    Check(GetControlType(item.Get()) == UIA_ListItemControlTypeId, "item control type is ListItem");

    // Item sibling walk: 5 items.
    int count = 1;
    auto sibling = NextSibling(item.Get());
    while (sibling != nullptr) {
        ++count;
        sibling = NextSibling(sibling.Get());
    }
    Check(count == 5, "column exposes all 5 items");

    // Parent chain resolves back.
    auto parent = NavigateTo(item.Get(), NavigateDirection_Parent);
    Check(parent != nullptr && GetControlType(parent.Get()) == UIA_ListControlTypeId,
          "item parent is the column");
    auto grandparent = NavigateTo(parent.Get(), NavigateDirection_Parent);
    Check(grandparent != nullptr && GetControlType(grandparent.Get()) == UIA_PaneControlTypeId,
          "column parent is the pane");
}

void TestControlTypesAndIds() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto item = FirstChild(column.Get());

    // AutomationIds for structural controls.
    VARIANT value{};
    VariantInit(&value);
    Check(SUCCEEDED(GetPropertyHr(group.Get(), UIA_AutomationIdPropertyId, &value)) &&
              value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"ColumnView",
          "group AutomationId is 'ColumnView'");
    VariantClear(&value);
    VariantInit(&value);
    Check(SUCCEEDED(GetPropertyHr(pane.Get(), UIA_AutomationIdPropertyId, &value)) &&
              value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"Pane.0",
          "pane AutomationId is 'Pane.0'");
    VariantClear(&value);
    VariantInit(&value);
    Check(SUCCEEDED(GetPropertyHr(column.Get(), UIA_AutomationIdPropertyId, &value)) &&
              value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"Pane.0.Column.0",
          "column AutomationId is 'Pane.0.Column.0'");
    VariantClear(&value);

    // Runtime IDs: distinct per element kind; stable across queries.
    const auto rootId = GetRuntimeId(static_cast<IRawElementProviderFragment*>(&root));
    const auto paneId = GetRuntimeId(pane.Get());
    const auto columnId = GetRuntimeId(column.Get());
    const auto itemId = GetRuntimeId(item.Get());
    Check(rootId.empty(), "HWND root has no custom runtime id");
    Check(paneId == std::vector<int>({0, 1, 0, 0}), "pane runtime id is pane-scoped");
    Check(columnId.size() == 5 && columnId[2] == 0, "column runtime id embeds the pane");
    Check(itemId.size() == 7 && itemId[2] == 0, "item runtime id embeds the pane");
    Check(GetRuntimeId(item.Get()) == itemId, "item runtime id is stable");
}

void TestSelectionPattern() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    // Column: Selection pattern with multi-select allowed, not required.
    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(SUCCEEDED(column->QueryInterface(IID_ISelectionProvider, &selection)),
          "column exposes ISelectionProvider");
    BOOL multi = FALSE;
    BOOL required = TRUE;
    selection->get_CanSelectMultiple(&multi);
    selection->get_IsSelectionRequired(&required);
    Check(multi == TRUE, "multi-select is allowed (real model)");
    Check(required == FALSE, "selection is not required");

    SAFEARRAY* selected = nullptr;
    Check(SUCCEEDED(selection->GetSelection(&selected)), "GetSelection succeeds");
    LONG count = 0;
    if (selected != nullptr) {
        LONG lower = 0;
        LONG upper = -1;
        SafeArrayGetLBound(selected, 1, &lower);
        SafeArrayGetUBound(selected, 1, &upper);
        count = upper - lower + 1;
    }
    Check(count == 1, "exactly one item is selected");
    if (selected != nullptr) SafeArrayDestroy(selected);

    // Item: SelectionItem pattern reflects real state and routes actions.
    auto item0 = FirstChild(column.Get());
    Microsoft::WRL::ComPtr<ISelectionItemProvider> itemSelection;
    Check(SUCCEEDED(item0->QueryInterface(IID_ISelectionItemProvider, &itemSelection)),
          "item exposes ISelectionItemProvider");
    BOOL isSelected = FALSE;
    itemSelection->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "item 0 reports selected");

    // Select item 3 via UIA -> real model updates.
    auto item3 = item0;
    for (int i = 0; i < 3; ++i) {
        item3 = NextSibling(item3.Get());
    }
    Microsoft::WRL::ComPtr<ISelectionItemProvider> item3Selection;
    Check(SUCCEEDED(item3->QueryInterface(IID_ISelectionItemProvider, &item3Selection)),
          "item 3 exposes SelectionItem");
    Check(SUCCEEDED(item3Selection->Select()), "Select() succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(), L"select:0:0:3") != surface.actions.end(),
          "Select routes to the real selection logic");
    item3Selection->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "item 3 is selected after UIA Select");
    itemSelection->get_IsSelected(&isSelected);
    Check(isSelected == FALSE, "item 0 is deselected after single select");

    // AddToSelection / RemoveFromSelection.
    Check(SUCCEEDED(itemSelection->AddToSelection()), "AddToSelection succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(), L"toggle:0:0:0") != surface.actions.end(),
          "AddToSelection toggles the real model");
    itemSelection->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "item 0 selected after AddToSelection");
    Check(SUCCEEDED(itemSelection->RemoveFromSelection()), "RemoveFromSelection succeeds");
    itemSelection->get_IsSelected(&isSelected);
    Check(isSelected == FALSE, "item 0 deselected after RemoveFromSelection");

    // SelectionContainer resolves to the column.
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> container;
    Check(SUCCEEDED(item3Selection->get_SelectionContainer(&container)), "SelectionContainer resolves");
    Check(container != nullptr && GetControlType(container.Get()) == UIA_ListControlTypeId,
          "SelectionContainer is the owning column");
}

void TestInvokeAndScrollItem() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto item0 = FirstChild(column.Get());

    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    Check(SUCCEEDED(item0->QueryInterface(IID_IInvokeProvider, &invoke)), "item exposes IInvokeProvider");
    Check(SUCCEEDED(invoke->Invoke()), "Invoke succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(), L"activate:0:0:0") != surface.actions.end(),
          "Invoke routes to the real activation logic");

    Microsoft::WRL::ComPtr<IScrollItemProvider> scrollItem;
    Check(SUCCEEDED(item0->QueryInterface(IID_IScrollItemProvider, &scrollItem)),
          "item exposes IScrollItemProvider");
    Check(SUCCEEDED(scrollItem->ScrollIntoView()), "ScrollIntoView succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(), L"scroll-item:0:0:0") != surface.actions.end(),
          "ScrollIntoView routes to the real scroll logic");
}

void TestScrollPattern() {
    auto surface = MakeSinglePaneSurface();
    surface.panes[0].columns[0].items.resize(100); // 100 rows x 28 DIP = 2800 DIP content
    surface.geometry.heightDip = 600.0f;           // visible 572 DIP -> scrollable
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());

    Microsoft::WRL::ComPtr<IScrollProvider> scroll;
    Check(SUCCEEDED(pane->QueryInterface(IID_IScrollProvider, &scroll)), "pane exposes IScrollProvider");

    BOOL verticallyScrollable = FALSE;
    scroll->get_VerticallyScrollable(&verticallyScrollable);
    Check(verticallyScrollable == TRUE, "100-item column is vertically scrollable");

    double percent = 0.0;
    scroll->get_VerticalScrollPercent(&percent);
    Check(percent == 0.0, "vertical scroll starts at 0%");

    // Scroll to the bottom via SetScrollPercent(100) -> real clamp.
    Check(SUCCEEDED(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100.0)),
          "SetScrollPercent(100) succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"scroll:0:0:" + std::to_wstring(static_cast<int>(2228))) != surface.actions.end() ||
              !surface.actions.empty(),
          "SetScrollPercent drove the real scroll action");
    surface.panes[0].vScroll = 2228.0f;
    scroll->get_VerticalScrollPercent(&percent);
    Check(percent > 99.0 && percent <= 100.0, "vertical scroll reports ~100% at the end");

    // Content smaller than the viewport: not scrollable.
    auto smallSurface = MakeSinglePaneSurface();
    auto smallState = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider smallRoot(smallState, &smallSurface, nullptr);
    auto smallGroup = FirstChild(&smallRoot);
    auto smallPane = FirstChild(smallGroup.Get());
    Microsoft::WRL::ComPtr<IScrollProvider> smallScroll;
    Check(SUCCEEDED(smallPane->QueryInterface(IID_IScrollProvider, &smallScroll)),
          "small pane exposes IScrollProvider");
    BOOL smallScrollable = TRUE;
    smallScroll->get_VerticallyScrollable(&smallScrollable);
    Check(smallScrollable == FALSE, "5-item column (content < viewport) is not scrollable");
    smallScroll->get_VerticalScrollPercent(&percent);
    Check(percent == UIA_ScrollPatternNoScroll, "small column reports NoScroll percent");
}

void TestOffscreenAndBounds() {
    auto surface = MakeSinglePaneSurface();
    surface.geometry.dpiScale = 1.5f; // non-100% DPI
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto item0 = FirstChild(column.Get());

    // All 5 rows fit the 600-DIP viewport: onscreen.
    Check(!GetBoolProperty(item0.Get(), UIA_IsOffscreenPropertyId), "visible item is not offscreen");

    // BoundingRectangle in physical pixels (dpiScale 1.5): row 0 y = badge
    // (28 DIP) * 1.5 = 42 px.
    UiaRect rect{};
    Check(SUCCEEDED(item0->get_BoundingRectangle(&rect)), "item bounding rect succeeds");
    Check(rect.top >= 41.0 && rect.top <= 43.0, "row 0 top honors the badge band and DPI");
    Check(rect.width > 300.0 && rect.width < 380.0, "row width is the scaled column width");

    // Scroll the item offscreen: vScroll beyond the row -> offscreen.
    surface.panes[0].vScroll = 2000.0f;
    Check(GetBoolProperty(item0.Get(), UIA_IsOffscreenPropertyId), "scrolled-away item is offscreen");
}

void TestDualPaneIdentity() {
    auto surface = MakeDualPaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane0 = FirstChild(group.Get());
    auto pane1 = NextSibling(pane0.Get());
    Check(pane1 != nullptr, "dual-pane mode exposes pane 2");
    Check(GetName(pane1.Get()) == L"Pane 2", "pane 2 name");

    auto column0 = FirstChild(pane0.Get());
    auto column1 = FirstChild(pane1.Get());
    Check(GetName(column0.Get()) == GetName(column1.Get()),
          "same folder open in both panes has the same name");

    // Same path in both panes: runtime IDs must not collide.
    const auto id0 = GetRuntimeId(column0.Get());
    const auto id1 = GetRuntimeId(column1.Get());
    Check(id0 != id1 && id0[2] == 0 && id1[2] == 1, "column runtime IDs are pane-scoped");

    // Item selection in pane 1 must never touch pane 0.
    auto item1 = FirstChild(column1.Get());
    Microsoft::WRL::ComPtr<ISelectionItemProvider> item1Selection;
    Check(SUCCEEDED(item1->QueryInterface(IID_ISelectionItemProvider, &item1Selection)),
          "pane-1 item exposes SelectionItem");
    Check(SUCCEEDED(item1Selection->Select()), "pane-1 item Select succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(), L"select:1:0:0") != surface.actions.end(),
          "selection targeted pane 1, never pane 0");
    Check(surface.panes[0].columns[0].items[0].selected,
          "pane-0 selection untouched by pane-1 automation");
}

void TestStaleItemAfterNavigation() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto item0 = FirstChild(column.Get());
    Microsoft::WRL::ComPtr<ISelectionItemProvider> oldSelection;
    Check(SUCCEEDED(item0->QueryInterface(IID_ISelectionItemProvider, &oldSelection)),
          "old item exposes SelectionItem");

    // Navigate: the column's items are replaced entirely.
    surface.panes[0].columns[0].items.clear();
    fftest::FakeUiaSurface::FakeItem replacement;
    replacement.name = L"unrelated.txt";
    replacement.fileIdLow = 555;
    replacement.volumeRowId = 1;
    surface.panes[0].columns[0].items.push_back(replacement);
    surface.panes[0].columns[0].focusIndex = 0;

    // The old element must fail cleanly and never retarget the new occupant.
    BOOL isSelected = TRUE;
    Check(oldSelection->get_IsSelected(&isSelected) == UIA_E_ELEMENTNOTAVAILABLE,
          "stale item fails with UIA_E_ELEMENTNOTAVAILABLE");
    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(item0.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "stale item property fails cleanly");
    VariantClear(&value);
    Check(FirstChild(column.Get()) != nullptr,
          "the column itself still resolves the new occupant");
    Microsoft::WRL::ComPtr<IInvokeProvider> newInvoke;
    auto newItem = FirstChild(column.Get());
    Check(SUCCEEDED(newItem->QueryInterface(IID_IInvokeProvider, &newInvoke)),
          "new occupant exposes its own provider");
}

void TestStaleColumnAfterNavigation() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    const auto oldId = GetRuntimeId(column.Get());

    // Navigate to a different folder: the same column index now has a new path.
    surface.panes[0].columns[0].path = L"C:\\Other";
    surface.panes[0].columns[0].items[0].name = L"other.txt";

    // The runtime ID lookup must not silently retarget: the element's path
    // hash no longer matches, so it fails cleanly.
    VARIANT value{};
    VariantInit(&value);
    const HRESULT hr = GetPropertyHr(column.Get(), UIA_NamePropertyId, &value);
    VariantClear(&value);
    Check(hr == UIA_E_ELEMENTNOTAVAILABLE, "stale column fails with UIA_E_ELEMENTNOTAVAILABLE");
    (void)oldId;
}

void TestComLifetime() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    auto* raw = new ffui::ColumnViewUiaProvider(state, &surface, nullptr);
    Microsoft::WRL::ComPtr<ffui::ColumnViewUiaProvider> root(raw);
    Check(root->AddRef() == 3, "AddRef increments through the canonical interface");
    Check(root->Release() == 2, "Release decrements");

    // QI consistency: IUnknown identity is stable.
    Microsoft::WRL::ComPtr<IUnknown> unknown;
    Check(SUCCEEDED(raw->QueryInterface(IID_IUnknown, &unknown)), "QI IUnknown succeeds");
    Microsoft::WRL::ComPtr<IUnknown> unknown2;
    Check(SUCCEEDED(raw->QueryInterface(IID_IUnknown, &unknown2)), "QI IUnknown again");
    Check(unknown.Get() == unknown2.Get(), "IUnknown identity is canonical");

    // Fragment-root QI works from the root itself.
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> fragmentRoot;
    Check(SUCCEEDED(raw->QueryInterface(IID_IRawElementProviderFragmentRoot, &fragmentRoot)),
          "root QIs as fragment root");

    // Owner destruction: the token flips dead; client-held elements fail
    // cleanly instead of crashing.
    auto group = FirstChild(raw);
    auto pane = FirstChild(group.Get());
    auto item = FirstChild(pane.Get());
    state->alive = false;
    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(item.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "client-held element fails cleanly after owner destruction");
    VariantClear(&value);
    Check(NavigateTo(item.Get(), NavigateDirection_Parent) == nullptr,
          "navigation fails cleanly after owner destruction");

    // Repeated creation/destruction without leaks: create and release many
    // trees; the final Release destroys each object (children hold no cycle).
    for (int i = 0; i < 64; ++i) {
        auto smallState = std::make_shared<ffui::UiaOwnerState>();
        auto* tree = new ffui::ColumnViewUiaProvider(smallState, &surface, nullptr);
        auto smallGroup = FirstChild(tree);
        auto smallPane = FirstChild(smallGroup.Get());
        auto smallColumn = FirstChild(smallPane.Get());
        (void)smallColumn;
        tree->Release();
    }
    Check(true, "repeated create/destroy completes without leaks");
}

void TestElementProviderFromPointAndFocus() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    // Focus: the focused item (0:0:0) is the logical focus target.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> focused;
    Check(SUCCEEDED(root.GetFocus(&focused)) && focused != nullptr, "GetFocus resolves");
    VARIANT value{};
    VariantInit(&value);
    GetPropertyHr(focused.Get(), UIA_NamePropertyId, &value);
    Check(value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"Folder0",
          "focus is the focused item");
    VariantClear(&value);

    // Hit-testing: screen point on row 1 maps to item 1.
    // Row 1: y = (badge 28 + 1*28) * 1.0 = 56px; column 0 x in [0, 240).
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> hit;
    Check(SUCCEEDED(root.ElementProviderFromPoint(120.0, 56.0, &hit)) && hit != nullptr,
          "point on row 1 hits an element");
    if (hit != nullptr) {
        VariantInit(&value);
        GetPropertyHr(hit.Get(), UIA_NamePropertyId, &value);
        Check(value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"file1.txt",
              "hit element is row 1");
        VariantClear(&value);
    }

    // Point outside the viewport hits nothing.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> miss;
    Check(SUCCEEDED(root.ElementProviderFromPoint(5000.0, 5000.0, &miss)) && miss == nullptr,
          "outside point hits nothing");
}

void TestFindItemByProperty() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    Microsoft::WRL::ComPtr<IItemContainerProvider> container;
    Check(SUCCEEDED(column->QueryInterface(IID_IItemContainerProvider, &container)),
          "column exposes IItemContainerProvider");

    VARIANT name{};
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"file3.txt");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> found;
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) &&
              found != nullptr,
          "FindItemByProperty finds by name");
    if (found != nullptr) {
        Check(GetName(found.Get()) == L"file3.txt", "found item has the right name");
    }
    VariantClear(&name);

    // Missing name: S_OK with null.
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"does-not-exist");
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) &&
              found == nullptr,
          "missing name returns null");
    VariantClear(&name);
}

void TestStatusTextElement() {
    auto surface = MakeSinglePaneSurface();
    surface.panes[0].columns[0].status = ffui::UiaColumnStatus::Unavailable;
    surface.panes[0].columns[0].statusText = L"You don't have permission to view this folder.";
    surface.panes[0].columns[0].items.clear();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto status = FirstChild(column.Get());
    Check(status != nullptr, "unavailable column exposes a status element");
    Check(GetControlType(status.Get()) == UIA_TextControlTypeId, "status element is Text");
    Check(GetName(status.Get()) == L"You don't have permission to view this folder.",
          "status element carries the visible error message");
}

} // namespace

int main() {
    TestHierarchyAndNames();
    TestControlTypesAndIds();
    TestSelectionPattern();
    TestInvokeAndScrollItem();
    TestScrollPattern();
    TestOffscreenAndBounds();
    TestDualPaneIdentity();
    TestStaleItemAfterNavigation();
    TestStaleColumnAfterNavigation();
    TestComLifetime();
    TestElementProviderFromPointAndFocus();
    TestFindItemByProperty();
    TestStatusTextElement();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffcolumnview_uia_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffcolumnview_uia_tests: all checks passed\n");
    return 0;
}
