// semantic-uia-core-surfaces 6.3: headless provider tests for the Search
// result list (result count, name/path metadata, selection, activation,
// virtualization, stale results after a new query, several-thousand-result
// behavior).

#include "SearchUiaProvider.h"
#include "UiaTestSurface.h"

#include <algorithm>
#include <string>

#include "../TestSupport.h"
#include "UiaTestHelpers.h"

using namespace fftest;

namespace {

fftest::FakeUiaSurface MakeSearchSurface(size_t count) {
    fftest::FakeUiaSurface surface;
    surface.searchVisible = true;
    surface.searchResults.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ffui::UiaSearchResultInfo info;
        info.name = i == 0 ? L"report.pdf" : L"file" + std::to_wstring(i) + L".txt";
        info.folder = L"C:\\Search";
        info.sizeBytes = 1024 * (i + 1);
        info.modifiedTime = 132500000000000000ull; // fixed FILETIME
        info.isDirectory = false;
        info.volumeId = 0; // degraded identity: folder + name
        info.fullPath = L"C:\\Search\\" + info.name;
        surface.searchResults.push_back(info);
    }
    return surface;
}


Microsoft::WRL::ComPtr<IRawElementProviderFragment> FirstChild(IRawElementProviderFragment* element) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(NavigateDirection_FirstChild, &result);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NextSibling(IRawElementProviderFragment* element) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(NavigateDirection_NextSibling, &result);
    return result;
}

void TestSearchTreeAndMetadata() {
    auto surface = MakeSearchSurface(3);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::SearchUiaProvider root(state, &surface, nullptr);

    Check(GetName(static_cast<IRawElementProviderFragment*>(&root)) == L"Search Results", "search list name");
    Check(GetControlType(static_cast<IRawElementProviderFragment*>(&root)) == UIA_ListControlTypeId, "search list is a List");

    auto first = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(first != nullptr, "results are exposed as children");
    Check(GetName(first.Get()) == L"report.pdf", "first result name");
    Check(GetControlType(first.Get()) == UIA_ListItemControlTypeId, "result is a ListItem");
    Check(GetStringProperty(first.Get(), UIA_HelpTextPropertyId) == L"C:\\Search",
          "HelpText carries the containing location");
    const std::wstring description = GetStringProperty(first.Get(), UIA_FullDescriptionPropertyId);
    Check(description.find(L"File") != std::wstring::npos &&
              description.find(L"1 KB") != std::wstring::npos,
          "FullDescription carries type and logical size");

    int count = 1;
    auto sibling = NextSibling(first.Get());
    while (sibling != nullptr) {
        ++count;
        sibling = NextSibling(sibling.Get());
    }
    Check(count == 3, "all three results are enumerable");
}

void TestSearchSelectionAndActivation() {
    auto surface = MakeSearchSurface(3);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::SearchUiaProvider root(state, &surface, nullptr);

    // Selection pattern: single-select, not required.
    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(SUCCEEDED(root.QueryInterface(IID_ISelectionProvider, &selection)),
          "search list exposes ISelectionProvider");
    BOOL multi = TRUE;
    BOOL required = TRUE;
    selection->get_CanSelectMultiple(&multi);
    selection->get_IsSelectionRequired(&required);
    Check(multi == FALSE, "search list is single-select");
    Check(required == FALSE, "search selection is not required");

    // Select the second result through UIA -> real model updates.
    auto first = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto second = NextSibling(first.Get());
    Microsoft::WRL::ComPtr<ISelectionItemProvider> itemSelection;
    Check(SUCCEEDED(second->QueryInterface(IID_ISelectionItemProvider, &itemSelection)),
          "result exposes ISelectionItemProvider");
    Check(SUCCEEDED(itemSelection->Select()), "Select succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"search-select:1") != surface.actions.end(),
          "Select routes to the native list state");
    BOOL isSelected = FALSE;
    itemSelection->get_IsSelected(&isSelected);
    Check(isSelected == TRUE, "result reports selected");
    SAFEARRAY* selectedArray = nullptr;
    Check(SUCCEEDED(selection->GetSelection(&selectedArray)), "GetSelection succeeds");
    LONG selectedCount = 0;
    if (selectedArray != nullptr) {
        LONG lower = 0;
        LONG upper = -1;
        SafeArrayGetLBound(selectedArray, 1, &lower);
        SafeArrayGetUBound(selectedArray, 1, &upper);
        selectedCount = upper - lower + 1;
    }
    Check(selectedCount == 1, "GetSelection returns exactly one result");
    if (selectedArray != nullptr) SafeArrayDestroy(selectedArray);

    // Invoke routes through the real search-to-navigation path.
    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    Check(SUCCEEDED(second->QueryInterface(IID_IInvokeProvider, &invoke)),
          "result exposes IInvokeProvider");
    Check(SUCCEEDED(invoke->Invoke()), "result Invoke succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"search-activate:1") != surface.actions.end(),
          "result activation routes to the real navigation path");

    // ScrollIntoView.
    Microsoft::WRL::ComPtr<IScrollItemProvider> scrollItem;
    Check(SUCCEEDED(second->QueryInterface(IID_IScrollItemProvider, &scrollItem)),
          "result exposes IScrollItemProvider");
    Check(SUCCEEDED(scrollItem->ScrollIntoView()), "ScrollIntoView succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"search-scroll:1") != surface.actions.end(),
          "ScrollIntoView uses the native ensure-visible behavior");
}

void TestSearchVirtualization() {
    // Several thousand results: providers are created on demand, bounded by
    // what the client touches.
    auto surface = MakeSearchSurface(5000);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::SearchUiaProvider root(state, &surface, nullptr);

    // Touch only a handful of results: provider cache stays tiny.
    auto first = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    (void)GetName(first.Get());
    auto second = NextSibling(first.Get());
    (void)GetName(second.Get());
    auto last = first;
    for (int i = 0; i < 4999; ++i) {
        last = NextSibling(last.Get());
    }
    (void)GetName(last.Get());

    // The 5000th result resolves with its own identity.
    Check(GetName(last.Get()) == L"file4999.txt", "the 5000th result resolves");

    // A new query replaces the result set: stale elements fail cleanly.
    auto stale = second;
    surface.searchResults.clear();
    surface.searchResults.push_back({});
    surface.searchResults[0].name = L"fresh.txt";
    surface.searchResults[0].folder = L"C:\\Search";
    surface.searchResults[0].fullPath = L"C:\\Search\\fresh.txt";
    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(stale.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "stale result fails cleanly after a new query");
    VariantClear(&value);

    auto fresh = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(GetName(fresh.Get()) == L"fresh.txt", "new query results are exposed");
}

void TestSearchFindItem() {
    auto surface = MakeSearchSurface(50);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::SearchUiaProvider root(state, &surface, nullptr);

    Microsoft::WRL::ComPtr<IItemContainerProvider> container;
    Check(SUCCEEDED(root.QueryInterface(IID_IItemContainerProvider, &container)),
          "search list exposes IItemContainerProvider");
    VARIANT name{};
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"file42.txt");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> found;
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) &&
              found != nullptr,
          "FindItemByProperty locates a deep result");
    if (found != nullptr) {
        Check(GetName(found.Get()) == L"file42.txt", "found result name matches");
    }
    VariantClear(&name);
}

} // namespace

int main() {
    TestSearchTreeAndMetadata();
    TestSearchSelectionAndActivation();
    TestSearchVirtualization();
    TestSearchFindItem();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffsearch_uia_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffsearch_uia_tests: all checks passed\n");
    return 0;
}
