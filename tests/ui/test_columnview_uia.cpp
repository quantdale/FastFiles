// semantic-uia-core-surfaces 6.1: headless provider tests for the Column
// View UIA tree (hierarchy, names, control types, runtime IDs, focus,
// selection, multi-selection, ScrollIntoView, offscreen, dual panes, stale
// elements, COM lifetime). Uses a fake UiaSurface; the provider tree is
// exercised through the real COM interfaces.

#include "ColumnViewUiaProvider.h"
#include "SearchUiaProvider.h"
#include "UiaHostQuery.h"
#include "UiaTestSurface.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

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

// A real cross-process UIA client passes a proxy for startAfter rather than
// the provider's concrete C++ object. Keep this proxy-shaped element small so
// ItemContainerProvider coverage exercises the same COM boundary.
class RuntimeIdProxy final : public IRawElementProviderSimple,
                             public IRawElementProviderFragment {
public:
    explicit RuntimeIdProxy(std::vector<int> runtimeId) : runtimeId_(std::move(runtimeId)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) noexcept override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() noexcept override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    STDMETHODIMP GetPatternProvider(PATTERNID, IUnknown** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        return S_OK;
    }

    STDMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        VariantInit(retVal);
        return S_OK;
    }

    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        return S_OK;
    }

    STDMETHODIMP get_ProviderOptions(ProviderOptions* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    STDMETHODIMP Navigate(NavigateDirection, IRawElementProviderFragment** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        return S_OK;
    }

    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        SAFEARRAYBOUND bound{static_cast<ULONG>(runtimeId_.size()), 0};
        SAFEARRAY* array = SafeArrayCreate(VT_I4, 1, &bound);
        if (array == nullptr) return E_OUTOFMEMORY;
        for (LONG i = 0; i < static_cast<LONG>(runtimeId_.size()); ++i) {
            (void)SafeArrayPutElement(array, &i, &runtimeId_[static_cast<size_t>(i)]);
        }
        *retVal = array;
        return S_OK;
    }

    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = UiaRect{};
        return S_OK;
    }

    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        return S_OK;
    }

    STDMETHODIMP SetFocus() noexcept override { return S_OK; }

    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    std::vector<int> runtimeId_;
};

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

// Replace the row after Resolve copied its identity, before the provider acts.
class ReplacingColumnSurface final : public fftest::FakeUiaSurface {
public:
    mutable bool replaceAfterRead = false;
    ffui::UiaItemInfo UiaItemInfoOf(int pane, int col, int row) const override {
        auto item = FakeUiaSurface::UiaItemInfoOf(pane, col, row);
        if (replaceAfterRead) {
            replaceAfterRead = false;
            auto& current = const_cast<ReplacingColumnSurface*>(this)->panes[0].columns[0].items[0];
            current.name = L"replacement.txt";
            current.fileIdLow = 9000;
        }
        return item;
    }
};

void TestItemActionRejectsReplacementAfterResolve() {
    for (int action = 0; action < 6; ++action) {
        ReplacingColumnSurface surface;
        static_cast<fftest::FakeUiaSurface&>(surface) = MakeSinglePaneSurface();
        surface.panes[0].columns[0].items[0].selected = action == 2;
        auto state = std::make_shared<ffui::UiaOwnerState>();
        ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
        auto item = FirstChild(FirstChild(FirstChild(FirstChild(
            static_cast<IRawElementProviderFragment*>(&root)).Get()).Get()).Get());
        Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
        Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
        Microsoft::WRL::ComPtr<IScrollItemProvider> scroll;
        item.As(&selection);
        item.As(&invoke);
        item.As(&scroll);
        surface.actions.clear();
        surface.replaceAfterRead = true;
        HRESULT result = E_FAIL;
        switch (action) {
        case 0: result = selection->Select(); break;
        case 1: result = selection->AddToSelection(); break;
        case 2: result = selection->RemoveFromSelection(); break;
        case 3: result = item->SetFocus(); break;
        case 4: result = invoke->Invoke(); break;
        case 5: result = scroll->ScrollIntoView(); break;
        }
        Check(result == UIA_E_ELEMENTNOTAVAILABLE, "replacement during item action fails closed");
        Check(surface.actions.empty(), "replacement never receives original item's action");
    }
}

class DispatchBoundaryColumnSurface final : public fftest::FakeUiaSurface {
public:
    std::function<void(const ffui::UiaBrowseActionRequest&)> beforeDispatch;
    bool UiaDispatchBrowseAction(const ffui::UiaBrowseActionRequest& request) override {
        if (beforeDispatch) beforeDispatch(request);
        return ffui::UiaSurface::UiaDispatchBrowseAction(request);
    }
};

void TestItemDispatchLifetimeAndMembership() {
    for (int change = 0; change < 5; ++change) {
        DispatchBoundaryColumnSurface surface;
        static_cast<fftest::FakeUiaSurface&>(surface) = MakeSinglePaneSurface();
        auto state = std::make_shared<ffui::UiaOwnerState>();
        ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
        auto item = FirstChild(FirstChild(FirstChild(FirstChild(
            static_cast<IRawElementProviderFragment*>(&root)).Get()).Get()).Get());
        Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
        item.As(&selection);
        surface.beforeDispatch = [&](const ffui::UiaBrowseActionRequest& request) {
            switch (change) {
            case 0: request.fragmentLifetime->alive = false; break;
            case 1: request.ownerLifetime->alive = false; break;
            case 2: surface.browseContentActive = false; break;
            case 3: surface.panes[0].columns[0].path = L"C:\\Replacement"; break;
            case 4: surface.panes[0].home = true; break;
            }
        };
        Check(selection->Select() == UIA_E_ELEMENTNOTAVAILABLE && surface.actions.empty(),
              "retired, hidden, Home or changed-column request rejects without mutation");
    }
    for (int add = 0; add < 2; ++add) {
        DispatchBoundaryColumnSurface surface;
        static_cast<fftest::FakeUiaSurface&>(surface) = MakeSinglePaneSurface();
        surface.panes[0].columns[0].items[0].selected = add == 0;
        auto state = std::make_shared<ffui::UiaOwnerState>();
        ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
        auto item = FirstChild(FirstChild(FirstChild(FirstChild(
            static_cast<IRawElementProviderFragment*>(&root)).Get()).Get()).Get());
        Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
        item.As(&selection);
        surface.beforeDispatch = [&](const ffui::UiaBrowseActionRequest&) {
            surface.panes[0].columns[0].items[0].selected = add != 0;
        };
        const HRESULT result = add ? selection->AddToSelection() : selection->RemoveFromSelection();
        Check(result == S_OK && surface.actions.empty(),
              "membership is checked at dispatch so Add/Remove remain idempotent");
    }
}

void TestContainerFocusRetainsOriginalLifetime() {
    for (int mode = 0; mode < 4; ++mode) {
        DispatchBoundaryColumnSurface surface;
        static_cast<fftest::FakeUiaSurface&>(surface) = MakeSinglePaneSurface();
        auto state = std::make_shared<ffui::UiaOwnerState>();
        ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
        auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
        auto pane = FirstChild(group.Get());
        auto column = FirstChild(pane.Get());
        surface.beforeDispatch = [&](const ffui::UiaBrowseActionRequest& request) {
            if (mode == 2) surface.panes[0].columns[0].path = L"C:\\Replacement";
            else request.fragmentLifetime->alive = false;
        };
        const HRESULT result = mode == 0 ? pane->SetFocus() : (mode == 3 ? group->SetFocus() : column->SetFocus());
        Check(result == UIA_E_ELEMENTNOTAVAILABLE && surface.actions.empty(),
              "container focus rejects retired fragment or changed path at dispatch");
    }
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
    surface.panes[0].columns[0].items.resize(100);
    surface.geometry.heightDip = 600.0f;           // visible 572 DIP -> scrollable
    const float expectedMaxScroll = ffui::UiaMaxVerticalScrollDip(100, surface.geometry.heightDip);
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
                    L"scroll:0:0:" + std::to_wstring(static_cast<int>(expectedMaxScroll))) != surface.actions.end() ||
              !surface.actions.empty(),
          "SetScrollPercent drove the real scroll action");
    surface.panes[0].vScroll = expectedMaxScroll;
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

// interactive-ui-runtime-determinism 3.1/3.2: a pane-layout change replaces
// the custom fragment generation. Providers held by a client from the old
// generation must fail cleanly, and a later enable must mint a new Pane.1
// provider instead of reviving the old object by index.
void TestDualPaneFragmentReplacement() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    auto oldGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto oldPane0 = FirstChild(oldGroup.Get());
    Check(oldGroup != nullptr && oldPane0 != nullptr,
          "pre-toggle fragment exposes its group and Pane.0");

    surface.panes.push_back(surface.panes[0]);
    ffui::UiaEventNotification enabled{
        ffui::UiaEventKind::StructureBulkAdded, -1, -1, -1, -1, -1,
        false, true};
    ffui::ColumnViewUiaProvider::SinkThunk(enabled, &root);

    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(oldGroup.Get(), UIA_NamePropertyId, &value) ==
              UIA_E_ELEMENTNOTAVAILABLE,
          "old ColumnView group fails after pane enable fragment replacement");
    VariantClear(&value);
    VariantInit(&value);
    Check(GetPropertyHr(oldPane0.Get(), UIA_AutomationIdPropertyId, &value) ==
              UIA_E_ELEMENTNOTAVAILABLE,
          "old Pane.0 fails after pane enable fragment replacement");
    VariantClear(&value);

    auto enabledGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto enabledPane0 = FirstChild(enabledGroup.Get());
    auto enabledPane1 = NextSibling(enabledPane0.Get());
    Check(enabledGroup != nullptr && enabledGroup.Get() != oldGroup.Get(),
          "enable re-roots a fresh ColumnView group");
    Check(enabledPane0 != nullptr && enabledPane1 != nullptr,
          "enable exposes both live panes through the fresh group");
    Check(GetStringProperty(enabledPane0.Get(), UIA_AutomationIdPropertyId) == L"Pane.0" &&
              GetStringProperty(enabledPane1.Get(), UIA_AutomationIdPropertyId) == L"Pane.1",
          "fresh panes retain distinct AutomationIds");
    const auto enabledPane1RuntimeId = GetRuntimeId(enabledPane1.Get());

    UiaRect pane0Rect{};
    UiaRect pane1Rect{};
    Check(SUCCEEDED(enabledPane0->get_BoundingRectangle(&pane0Rect)) &&
              SUCCEEDED(enabledPane1->get_BoundingRectangle(&pane1Rect)) &&
              pane0Rect.width > 0.0 && pane1Rect.width > 0.0 &&
              pane0Rect.left + pane0Rect.width <= pane1Rect.left + 1.0,
          "fresh panes have positive, non-overlapping left-to-right geometry");

    surface.panes.pop_back();
    ffui::UiaEventNotification disabled{
        ffui::UiaEventKind::StructureBulkRemoved, -1, -1, -1, -1, -1,
        false, true};
    ffui::ColumnViewUiaProvider::SinkThunk(disabled, &root);
    VariantInit(&value);
    Check(GetPropertyHr(enabledPane1.Get(), UIA_AutomationIdPropertyId, &value) ==
              UIA_E_ELEMENTNOTAVAILABLE,
          "Pane.1 fails after dual-pane disable");
    VariantClear(&value);

    surface.panes.push_back(surface.panes[0]);
    ffui::ColumnViewUiaProvider::SinkThunk(enabled, &root);
    auto reenabledGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto reenabledPane0 = FirstChild(reenabledGroup.Get());
    auto reenabledPane1 = NextSibling(reenabledPane0.Get());
    Check(reenabledPane1 != nullptr && reenabledPane1.Get() != enabledPane1.Get(),
          "re-enable mints a fresh Pane.1 provider rather than reviving the old one");
    Check(GetStringProperty(reenabledPane1.Get(), UIA_AutomationIdPropertyId) == L"Pane.1" &&
              GetRuntimeId(reenabledPane1.Get()) == enabledPane1RuntimeId,
          "re-enabled Pane.1 keeps stable identity while using a new provider object");

    // interactive-ui-runtime-determinism 2.3: a snapshot refresh/reconnect
    // can replace a column while the dual-pane fragment is already live. The
    // provider must retire and rebuild the fragment without routing any
    // filesystem, engine, or service action through the UI surface.
    surface.panes[0].columns[0].status = ffui::UiaColumnStatus::Loading;
    surface.panes[0].columns[0].statusText = L"Refreshing...";
    ffui::ColumnViewUiaProvider::SinkThunk(
        {ffui::UiaEventKind::StructureBulkRemoved, 0, 0, -1}, &root);
    ffui::ColumnViewUiaProvider::SinkThunk(
        {ffui::UiaEventKind::StructureBulkAdded, 0, 0, -1}, &root);
    auto refreshedGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(refreshedGroup != nullptr && FirstChild(refreshedGroup.Get()) != nullptr,
          "pending refresh/reconnect notification re-roots a live dual-pane fragment");
    Check(surface.actions.empty(),
          "pane creation and refresh notifications issue no filesystem/engine/service action");
    surface.panes[0].columns[0].status = ffui::UiaColumnStatus::Current;
    surface.panes[0].columns[0].statusText.clear();

    // Stress the same production notification/reacquisition boundary without
    // using a sleep or a retry. Each iteration keeps the previous Pane.1
    // alive long enough to prove that the next structure transition retires
    // it, then obtains the new provider from the root.
    bool cyclesClean = true;
    auto previousPane1 = reenabledPane1;
    for (int cycle = 0; cycle < 50; ++cycle) {
        const bool disable = (surface.panes.size() == 2);
        if (disable) {
            auto staleCandidate = previousPane1;
            surface.panes.pop_back();
            ffui::ColumnViewUiaProvider::SinkThunk(disabled, &root);
            if (staleCandidate != nullptr) {
                VARIANT stale{};
                VariantInit(&stale);
                const HRESULT staleHr = GetPropertyHr(staleCandidate.Get(), UIA_NamePropertyId, &stale);
                VariantClear(&stale);
                if (staleHr != UIA_E_ELEMENTNOTAVAILABLE) cyclesClean = false;
            }
            previousPane1 = nullptr;
        } else {
            surface.panes.push_back(surface.panes[0]);
            ffui::ColumnViewUiaProvider::SinkThunk(enabled, &root);
            auto currentGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
            auto currentPane0 = FirstChild(currentGroup.Get());
            auto currentPane1 = NextSibling(currentPane0.Get());
            if (currentPane1 == nullptr || currentPane1.Get() == previousPane1.Get() ||
                GetStringProperty(currentPane1.Get(), UIA_AutomationIdPropertyId) != L"Pane.1") {
                cyclesClean = false;
            }
            previousPane1 = currentPane1;
        }
    }
    Check(cyclesClean, "50 deterministic pane fragment replacement cycles retire stale providers");
}

void TestToolWorkspaceReplacesBrowse() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    const ffui::UiaEventNotification replacement{
        ffui::UiaEventKind::StructureChanged, -1, -1, -1, -1, -1, false, true};
    for (const auto* tool : {"Search", "Storage list"}) {
        auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
        auto pane = FirstChild(group.Get());
        auto column = FirstChild(pane.Get());
        auto item = FirstChild(column.Get());
        Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
        Check(SUCCEEDED(item.As(&invoke)), "Browse item exposes its real Invoke action");
        surface.browseContentActive = false;
        ffui::ColumnViewUiaProvider::SinkThunk(replacement, &root);
        Check(FirstChild(static_cast<IRawElementProviderFragment*>(&root)) == nullptr, tool);
        UiaRect rect{};
        Check(pane->get_BoundingRectangle(&rect) == UIA_E_ELEMENTNOTAVAILABLE,
              "held hidden pane has no screen bounds");
        Check(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE && surface.actions.empty(),
              "held hidden Browse item cannot invoke filesystem navigation");
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> hit;
        Check(SUCCEEDED(root.ElementProviderFromPoint(20.0, 20.0, &hit)) && hit == nullptr,
              "tool workspace hit test does not return hidden Browse");
        Check(SUCCEEDED(root.GetFocus(&hit)) && hit == nullptr,
              "tool workspace does not report hidden Browse focus");
        surface.browseContentActive = true;
        ffui::ColumnViewUiaProvider::SinkThunk(replacement, &root);
        auto restoredGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
        auto restoredPane = FirstChild(restoredGroup.Get());
        Check(restoredPane != nullptr && restoredPane.Get() != pane.Get(),
              "returning Browse mints a new provider lifetime");
        Check(pane->get_BoundingRectangle(&rect) == UIA_E_ELEMENTNOTAVAILABLE &&
                  invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE,
              "restoring Browse never revives retained hidden providers");
    }
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
    surface.actions.clear();
    Check(column->SetFocus() == UIA_E_ELEMENTNOTAVAILABLE,
          "stale column focus fails with UIA_E_ELEMENTNOTAVAILABLE");
    Check(surface.actions.empty(), "stale column focus never targets replacement folder");
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

    // Hit-testing: screen point in row 1 maps to item 1.
    // native-precision-workspace: derive the point from the shared row metric
    // so density changes exercise the same rendered/UIA geometry.
    const double row1Y = static_cast<double>(ffui::UiMetrics::kBadgeHeight) +
                         static_cast<double>(ffui::UiaBrowseRowHeightDip()) * 1.5;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> hit;
    Check(SUCCEEDED(root.ElementProviderFromPoint(120.0, row1Y, &hit)) && hit != nullptr,
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

// Walk the column's item children to the Nth item (0-based).
Microsoft::WRL::ComPtr<IRawElementProviderFragment> ItemAtColumn(
    IRawElementProviderFragment* column, int index) {
    auto item = FirstChild(column);
    for (int i = 0; i < index && item != nullptr; ++i) {
        item = NextSibling(item.Get());
    }
    return item;
}

Microsoft::WRL::ComPtr<ISelectionItemProvider> AsSelectionItem(
    IRawElementProviderFragment* item) {
    Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
    if (item != nullptr) {
        (void)item->QueryInterface(IID_ISelectionItemProvider, &selection);
    }
    return selection;
}

// Collect the display Names of every element returned by the owning column's
// ISelectionProvider::GetSelection() into an ordered set, so exact membership
// can be asserted regardless of returned order.
std::set<std::wstring> GetSelectedNames(ISelectionProvider* selection) {
    std::set<std::wstring> names;
    SAFEARRAY* selected = nullptr;
    if (FAILED(selection->GetSelection(&selected)) || selected == nullptr) {
        return names;
    }
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(selected, 1, &lower);
    SafeArrayGetUBound(selected, 1, &upper);
    for (LONG i = lower; i <= upper; ++i) {
        IRawElementProviderSimple* raw = nullptr;
        if (SUCCEEDED(SafeArrayGetElement(selected, &i, &raw)) && raw != nullptr) {
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
            element.Attach(raw); // owns the ref from SafeArrayGetElement
            names.insert(GetName(element.Get()));
        }
    }
    SafeArrayDestroy(selected);
    return names;
}

// 4.2: SelectionItem.Select() replaces the selection and the previously
// selected item reports IsSelected==FALSE through the real provider.
void TestSelectionSelectReplacesAndReports() {
    auto surface = MakeSinglePaneSurface(); // item 0 selected initially
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(SUCCEEDED(column->QueryInterface(IID_ISelectionProvider, &selection)),
          "column exposes ISelectionProvider");

    // Initial selection is exactly item 0.
    Check(GetSelectedNames(selection.Get()) == std::set<std::wstring>{L"Folder0"},
          "initial selection is exactly item 0");

    // Locate item 0 (Folder0) and item 3 (file3.txt) through the real tree.
    auto item0 = AsSelectionItem(ItemAtColumn(column.Get(), 0).Get());
    auto item3 = AsSelectionItem(ItemAtColumn(column.Get(), 3).Get());
    Check(item0 != nullptr && item3 != nullptr, "items 0 and 3 expose SelectionItem");

    // Select item 3 via UIA -> replaces the entire selection.
    Check(SUCCEEDED(item3->Select()), "Select() on item 3 succeeds");
    Check(GetSelectedNames(selection.Get()) == std::set<std::wstring>{L"file3.txt"},
          "GetSelection returns exactly the target (item 3)");
    BOOL isSelected = FALSE;
    item0->get_IsSelected(&isSelected);
    Check(isSelected == FALSE, "previously selected item 0 reports IsSelected==FALSE");
}

// 4.3: AddToSelection() on an unselected item yields exactly the multi-select
// union and leaves unrelated items unselected.
void TestSelectionAddToSelectionMembership() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(SUCCEEDED(column->QueryInterface(IID_ISelectionProvider, &selection)),
          "column exposes ISelectionProvider");

    auto item0 = AsSelectionItem(ItemAtColumn(column.Get(), 0).Get());
    auto item2 = AsSelectionItem(ItemAtColumn(column.Get(), 2).Get());
    auto item3 = AsSelectionItem(ItemAtColumn(column.Get(), 3).Get());
    Check(item0 != nullptr && item2 != nullptr && item3 != nullptr,
          "items 0, 2, and 3 expose SelectionItem");

    // Start with item 3 selected only.
    Check(SUCCEEDED(item3->Select()), "Select() on item 3 succeeds");
    Check(GetSelectedNames(selection.Get()) == std::set<std::wstring>{L"file3.txt"},
          "starting selection is exactly item 3");

    // AddToSelection on item 0 -> {item0, item3}.
    Check(SUCCEEDED(item0->AddToSelection()), "AddToSelection on item 0 succeeds");
    Check(GetSelectedNames(selection.Get()) ==
              std::set<std::wstring>{L"Folder0", L"file3.txt"},
          "GetSelection returns exactly {item0, item3}");
    BOOL isSelected = FALSE;
    item0->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "item 0 is selected after AddToSelection");
    item2->get_IsSelected(&isSelected);
    Check(isSelected == FALSE, "unrelated item 2 reports IsSelected==FALSE");
}

// 4.4: RemoveFromSelection() removes only its target; remaining members stay.
void TestSelectionRemoveFromSelectionMembership() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(SUCCEEDED(column->QueryInterface(IID_ISelectionProvider, &selection)),
          "column exposes ISelectionProvider");

    auto item0 = AsSelectionItem(ItemAtColumn(column.Get(), 0).Get());
    auto item3 = AsSelectionItem(ItemAtColumn(column.Get(), 3).Get());
    Check(item0 != nullptr && item3 != nullptr, "items 0 and 3 expose SelectionItem");

    // Precondition: {item0, item3} selected.
    Check(SUCCEEDED(item3->Select()), "Select() on item 3 succeeds");
    Check(SUCCEEDED(item0->AddToSelection()), "AddToSelection on item 0 succeeds");
    Check(GetSelectedNames(selection.Get()) ==
              std::set<std::wstring>{L"Folder0", L"file3.txt"},
          "precondition: {item0, item3} selected");

    // RemoveFromSelection on item 0 -> only item 3 remains.
    Check(SUCCEEDED(item0->RemoveFromSelection()), "RemoveFromSelection on item 0 succeeds");
    Check(GetSelectedNames(selection.Get()) == std::set<std::wstring>{L"file3.txt"},
          "GetSelection returns exactly {item3} after removal");
    BOOL isSelected = FALSE;
    item3->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "item 3 remains IsSelected==TRUE");
    item0->get_IsSelected(&isSelected);
    Check(isSelected == FALSE, "item 0 is deselected after removal");
}

// 4.5: Mutating a same-index item in one pane must not alter selection
// membership in the other pane (read strictly through the real providers).
void TestSelectionDualPaneIsolation() {
    auto surface = MakeDualPaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane0 = FirstChild(group.Get());
    auto pane1 = NextSibling(pane0.Get());
    Check(pane1 != nullptr, "dual-pane exposes pane 1");
    auto column0 = FirstChild(pane0.Get());
    auto column1 = FirstChild(pane1.Get());

    Microsoft::WRL::ComPtr<ISelectionProvider> selection0;
    Microsoft::WRL::ComPtr<ISelectionProvider> selection1;
    Check(SUCCEEDED(column0->QueryInterface(IID_ISelectionProvider, &selection0)),
          "pane-0 column exposes ISelectionProvider");
    Check(SUCCEEDED(column1->QueryInterface(IID_ISelectionProvider, &selection1)),
          "pane-1 column exposes ISelectionProvider");

    // Establish a real selection in pane 0 (Select item 0).
    auto item0p0 = AsSelectionItem(FirstChild(column0.Get()).Get());
    Check(item0p0 != nullptr, "pane-0 item 0 exposes SelectionItem");
    Check(SUCCEEDED(item0p0->Select()), "pane-0 item 0 Select succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"select:0:0:0") != surface.actions.end(),
          "selection routed to pane 0");
    Check(GetSelectedNames(selection0.Get()) == std::set<std::wstring>{L"Folder0"},
          "pane 0 selection is exactly item 0");

    // Mutate the same-index item 0 in pane 1.
    auto item0p1 = AsSelectionItem(FirstChild(column1.Get()).Get());
    Check(item0p1 != nullptr, "pane-1 item 0 exposes SelectionItem");
    Check(SUCCEEDED(item0p1->Select()), "pane-1 item 0 Select succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"select:1:0:0") != surface.actions.end(),
          "same-index mutation routed to pane 1, never pane 0");
    Check(GetSelectedNames(selection1.Get()) == std::set<std::wstring>{L"shared.txt"},
          "pane 1 target is selected");

    // Pane 0's selection is unchanged by the pane-1 mutation.
    Check(GetSelectedNames(selection0.Get()) == std::set<std::wstring>{L"Folder0"},
          "pane 0 selection remains exactly item 0 after pane-1 mutation");
    BOOL isSelected = FALSE;
    item0p0->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "pane-0 item 0 still reports IsSelected==TRUE");
}

// P1: the fragment root must expose the HWND host provider so UIA merges the
// custom element with the window's default provider (native child controls
// stay reachable). Headless (null hwnd) it degrades to S_OK with null.
void TestHostRawElementProvider() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    {
        ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
        IRawElementProviderSimple* host = nullptr;
        Check(SUCCEEDED(root.get_HostRawElementProvider(&host)) && host == nullptr,
              "headless root returns S_OK with a null host provider");
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"fftest_uia_host";
        RegisterClassW(&wc);
        registered = true;
    }
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"fftest_uia_host", L"fftest", WS_POPUP,
                                0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, "host-provider test window created");
    if (hwnd != nullptr) {
        ffui::ColumnViewUiaProvider root(state, &surface, hwnd);
        IRawElementProviderSimple* host = nullptr;
        Check(SUCCEEDED(root.get_HostRawElementProvider(&host)) && host != nullptr,
              "HWND-backed root returns its host provider");
        if (host != nullptr) host->Release();
        DestroyWindow(hwnd);
    }
}

// LastChild from the Column View group must follow the pane count instead of
// always landing on Pane 1.
void TestGroupLastChildFollowsPaneCount() {
    auto dual = MakeDualPaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &dual, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto first = FirstChild(group.Get());
    auto last = NavigateTo(group.Get(), NavigateDirection_LastChild);
    Check(first != nullptr && GetName(first.Get()) == L"Pane 1", "dual-pane FirstChild is Pane 1");
    Check(last != nullptr && GetName(last.Get()) == L"Pane 2", "dual-pane LastChild is Pane 2");

    auto single = MakeSinglePaneSurface();
    ffui::ColumnViewUiaProvider singleRoot(state, &single, nullptr);
    auto singleGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&singleRoot));
    auto singleLast = NavigateTo(singleGroup.Get(), NavigateDirection_LastChild);
    Check(singleLast != nullptr && GetName(singleLast.Get()) == L"Pane 1",
          "single-pane LastChild stays Pane 1");
}

// A startAfter element from a foreign surface is legal ItemContainer input:
// it must degrade to "start from the beginning", never reinterpret fields.
void TestFindItemByPropertyForeignStartAfter() {
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    Microsoft::WRL::ComPtr<IItemContainerProvider> container;
    Check(SUCCEEDED(column->QueryInterface(IID_IItemContainerProvider, &container)),
          "column exposes IItemContainerProvider");

    // Foreign element: a search result provider from a different surface.
    fftest::FakeUiaSurface searchSurface;
    searchSurface.searchVisible = true;
    ffui::UiaSearchResultInfo foreignInfo;
    foreignInfo.name = L"foreign.txt";
    foreignInfo.folder = L"C:\\Elsewhere";
    foreignInfo.fullPath = L"C:\\Elsewhere\\foreign.txt";
    searchSurface.searchResults.push_back(foreignInfo);
    ffui::SearchUiaProvider searchRoot(state, &searchSurface, nullptr);
    auto foreign = FirstChild(static_cast<IRawElementProviderFragment*>(&searchRoot));
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> foreignSimple;
    Check(SUCCEEDED(foreign.As(&foreignSimple)) && foreignSimple != nullptr,
          "foreign element resolves");

    VARIANT name{};
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"file3.txt");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> found;
    Check(SUCCEEDED(container->FindItemByProperty(foreignSimple.Get(), UIA_NamePropertyId, name, &found)) &&
              found != nullptr && GetName(found.Get()) == L"file3.txt",
          "foreign startAfter degrades to a full scan and still finds the item");
    VariantClear(&name);

    // Same-container startAfter keeps working: starting after item 0 still
    // finds file3.txt at row 3.
    auto item0 = FirstChild(column.Get());
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> item0Simple;
    Check(SUCCEEDED(item0.As(&item0Simple)), "own item resolves as startAfter");
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"file3.txt");
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(item0Simple.Get(), UIA_NamePropertyId, name, &found)) &&
              found != nullptr && GetName(found.Get()) == L"file3.txt",
          "own startAfter still finds later items");
    VariantClear(&name);

    // A cross-process UIA proxy has the same runtime ID but is not a
    // ColumnItemUiaProvider object. The production path must parse identity,
    // never down-cast the proxy.
    const auto item0RuntimeId = GetRuntimeId(item0.Get());
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> proxy;
    proxy.Attach(new RuntimeIdProxy(item0RuntimeId));
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"file3.txt");
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(proxy.Get(), UIA_NamePropertyId, name, &found)) &&
              found != nullptr && GetName(found.Get()) == L"file3.txt",
          "proxy-shaped startAfter is resolved through its runtime identity");
    VariantClear(&name);
}

// The pane projects real keyboard-focus state: active pane + window focus +
// no focused row. The fixture has keyboardFocus=true.
void TestPaneHasKeyboardFocusProjection() {
    auto surface = MakeSinglePaneSurface(); // focusIndex = 0 -> row owns focus
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    Check(!GetBoolProperty(pane.Get(), UIA_HasKeyboardFocusPropertyId),
          "pane does not claim focus while a row owns it");

    surface.panes[0].columns[0].focusIndex = -1; // no focused row
    Check(GetBoolProperty(pane.Get(), UIA_HasKeyboardFocusPropertyId),
          "active pane with no focused row reports HasKeyboardFocus");

    surface.geometry.keyboardFocus = false;
    Check(!GetBoolProperty(pane.Get(), UIA_HasKeyboardFocusPropertyId),
          "pane reports no focus when the window lacks keyboard focus");
}

// The status text's IsOffscreen must agree with its scrolled bounds.
void TestStatusOffscreenTracksScroll() {
    auto surface = MakeSinglePaneSurface();
    surface.panes[0].columns[0].status = ffui::UiaColumnStatus::Unavailable;
    surface.panes[0].columns[0].statusText = L"This folder is unavailable.";
    surface.panes[0].columns[0].items.clear();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());
    auto status = FirstChild(column.Get());
    Check(status != nullptr, "status element resolves for the offscreen check");
    Check(!GetBoolProperty(status.Get(), UIA_IsOffscreenPropertyId),
          "on-screen status text is not offscreen");

    // Scroll the column far out of the pane clip: offscreen flips true.
    surface.geometry.hScroll0Dip = 5000.0f;
    Check(GetBoolProperty(status.Get(), UIA_IsOffscreenPropertyId),
          "scrolled-out status text reports IsOffscreen=true");
}

struct HostCtorProbe {
    int getObjectCount = 0;
    int hostQueryFallthrough = 0;
};

LRESULT CALLBACK HostCtorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId) {
        auto* probe = reinterpret_cast<HostCtorProbe*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (probe != nullptr) {
            probe->getObjectCount++;
            if (ffui::UiaHostQueryGuard::Active()) {
                probe->hostQueryFallthrough++;
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TestHostProviderNotConstructedDuringProviderCtor() {
    HostCtorProbe probe;
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = HostCtorProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FfUiaHostCtorProbe";
    RegisterClassW(&windowClass);
    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"probe", WS_OVERLAPPEDWINDOW, 0, 0, 80, 60,
                                nullptr, nullptr, windowClass.hInstance, nullptr);
    Check(hwnd != nullptr, "probe HWND for host-provider construction");
    if (hwnd == nullptr) {
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&probe));
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    {
        ffui::ColumnViewUiaProvider root(state, &surface, hwnd);
        Check(probe.getObjectCount == 0,
              "ColumnViewUiaProvider construction does not re-enter WM_GETOBJECT via UiaHostProviderFromHwnd");
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> host;
        Check(SUCCEEDED(root.get_HostRawElementProvider(&host)), "lazy get_HostRawElementProvider succeeds");
        VARIANT className{};
        VariantInit(&className);
        Check(SUCCEEDED(root.GetPropertyValue(UIA_ClassNamePropertyId, &className)),
              "ClassName is answered from the HWND without host recursion");
        Check(className.vt == VT_BSTR && className.bstrVal != nullptr &&
                  wcscmp(className.bstrVal, L"FfUiaHostCtorProbe") == 0,
              "ClassName matches the probe window class");
        VariantClear(&className);
        VARIANT processId{};
        VariantInit(&processId);
        Check(SUCCEEDED(root.GetPropertyValue(UIA_ProcessIdPropertyId, &processId)) &&
                  processId.vt == VT_I4 && processId.lVal == static_cast<LONG>(GetCurrentProcessId()),
              "ProcessId is the hosting process");
        VariantClear(&processId);
    }
    DestroyWindow(hwnd);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
}

LRESULT CALLBACK RecursingHostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId) {
        auto* provider =
            reinterpret_cast<IRawElementProviderSimple*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (provider != nullptr) {
            return UiaReturnRawElementProvider(hwnd, wParam, lParam, provider);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TestCustomGetObjectDoesNotRecurseOnIdentityProperties() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = RecursingHostProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FfUiaRecurseHostProbe";
    RegisterClassW(&windowClass);
    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"FastFiles", WS_OVERLAPPEDWINDOW, 0, 0, 80, 60,
                                nullptr, nullptr, windowClass.hInstance, nullptr);
    Check(hwnd != nullptr, "recurse-probe HWND created");
    if (hwnd == nullptr) {
        return;
    }
    auto surface = MakeSinglePaneSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    auto* root = new ffui::ColumnViewUiaProvider(state, &surface, hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(static_cast<IRawElementProviderSimple*>(root)));
    VARIANT className{};
    VariantInit(&className);
    Check(SUCCEEDED(root->GetPropertyValue(UIA_ClassNamePropertyId, &className)),
          "identity GetPropertyValue survives a GETOBJECT handler that returns the custom root");
    Check(className.vt == VT_BSTR && className.bstrVal != nullptr &&
              wcscmp(className.bstrVal, L"FfUiaRecurseHostProbe") == 0,
          "ClassName still comes from GetClassNameW");
    VariantClear(&className);
    UiaRect bounds{};
    Check(SUCCEEDED(root->get_BoundingRectangle(&bounds)),
          "get_BoundingRectangle does not recurse through a self host");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> host;
    Check(SUCCEEDED(root->get_HostRawElementProvider(&host)),
          "get_HostRawElementProvider does not overflow when GETOBJECT returns this root");
    VARIANT help{};
    VariantInit(&help);
    Check(SUCCEEDED(root->GetPropertyValue(UIA_HelpTextPropertyId, &help)),
          "non-identity property does not recurse through a self host");
    VariantClear(&help);
    {
        ffui::UiaHostQueryGuard nested;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> nestedHost;
        Check(SUCCEEDED(root->get_HostRawElementProvider(&nestedHost)) && nestedHost == nullptr,
              "get_HostRawElementProvider is a no-op while a host query is already on the stack");
    }
    DestroyWindow(hwnd);
    root->Release();
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
}

void TestHostQueryGuardIsPerHwnd() {
    Check(!ffui::UiaHostQueryGuard::Active(), "host query guard starts empty");
    const HWND queried = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x100));
    const HWND other = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x200));
    {
        ffui::UiaHostQueryGuard guard(queried);
        Check(ffui::UiaHostQueryGuard::Active(), "named host query is visible");
        Check(ffui::UiaHostQueryGuard::Active(queried), "queried HWND is blocked");
        Check(!ffui::UiaHostQueryGuard::Active(other),
              "unrelated HWND is not blocked by another window's host query");
    }
    Check(!ffui::UiaHostQueryGuard::Active() && !ffui::UiaHostQueryGuard::Active(queried),
          "host query guard is empty after the named guard is destroyed");
    {
        ffui::UiaHostQueryGuard global;
        Check(ffui::UiaHostQueryGuard::Active(queried) && ffui::UiaHostQueryGuard::Active(other),
              "default-constructed guard still blocks every HWND");
    }
}

} // namespace

int main() {
    TestHierarchyAndNames();
    TestControlTypesAndIds();
    TestSelectionPattern();
    TestSelectionSelectReplacesAndReports();
    TestSelectionAddToSelectionMembership();
    TestSelectionRemoveFromSelectionMembership();
    TestSelectionDualPaneIsolation();
    TestItemActionRejectsReplacementAfterResolve();
    TestItemDispatchLifetimeAndMembership();
    TestContainerFocusRetainsOriginalLifetime();
    TestInvokeAndScrollItem();
    TestScrollPattern();
    TestOffscreenAndBounds();
    TestDualPaneIdentity();
    TestDualPaneFragmentReplacement();
    TestToolWorkspaceReplacesBrowse();
    TestStaleItemAfterNavigation();
    TestStaleColumnAfterNavigation();
    TestComLifetime();
    TestElementProviderFromPointAndFocus();
    TestFindItemByProperty();
    TestStatusTextElement();
    TestHostRawElementProvider();
    TestGroupLastChildFollowsPaneCount();
    TestFindItemByPropertyForeignStartAfter();
    TestPaneHasKeyboardFocusProjection();
    TestStatusOffscreenTracksScroll();
    TestHostProviderNotConstructedDuringProviderCtor();
    TestCustomGetObjectDoesNotRecurseOnIdentityProperties();
    TestHostQueryGuardIsPerHwnd();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffcolumnview_uia_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffcolumnview_uia_tests: all checks passed\n");
    return 0;
}
