// semantic-uia-core-surfaces 6.2: headless provider tests for the Home
// surface (sections, items, Clear buttons, quick actions, disabled known
// folders, focus parity, stale elements after refresh).

#include "ColumnViewUiaProvider.h"
#include "UiaTestSurface.h"

#include <algorithm>
#include <string>

#include "../TestSupport.h"
#include "UiaTestHelpers.h"

using namespace fftest;

namespace {

fftest::FakeUiaSurface MakeHomeSurface() {
    fftest::FakeUiaSurface surface;
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    surface.geometry.dpiScale = 1.0f;
    surface.geometry.keyboardFocus = true;
    fftest::FakeUiaSurface::FakePane pane;
    pane.home = true;
    surface.panes.push_back(pane);

    // Pinned section: one pinned folder.
    fftest::FakeUiaSurface::FakeHomeSection pinned;
    pinned.sectionKind = ffui::kUiaHomeSectionPinned;
    pinned.itemCount = 1;
    pinned.firstGlobalIndex = 0;
    surface.homeSections.push_back(pinned);
    fftest::FakeUiaSurface::FakeHomeItem pinnedItem;
    pinnedItem.sectionKind = ffui::kUiaHomeSectionPinned;
    pinnedItem.indexInSection = 0;
    pinnedItem.primary = L"Documents";
    pinnedItem.secondary = L"C:\\Users\\Test\\Documents";
    pinnedItem.value = L"C:\\Users\\Test\\Documents";
    surface.homeItems.push_back(pinnedItem);

    // Recent folders: Clear affordance + two items.
    fftest::FakeUiaSurface::FakeHomeSection recent;
    recent.sectionKind = ffui::kUiaHomeSectionRecentFolders;
    recent.itemCount = 2;
    recent.firstGlobalIndex = 1;
    recent.clearAffordance = true;
    recent.headerY = 60.0f;
    surface.homeSections.push_back(recent);
    for (int i = 0; i < 2; ++i) {
        fftest::FakeUiaSurface::FakeHomeItem item;
        item.sectionKind = ffui::kUiaHomeSectionRecentFolders;
        item.indexInSection = i;
        item.primary = L"C:\\Recent" + std::to_wstring(i);
        item.value = item.primary;
        item.y = 100.0f + static_cast<float>(i) * 50.0f;
        surface.homeItems.push_back(item);
    }

    // Common locations: one available, one unavailable (disabled).
    fftest::FakeUiaSurface::FakeHomeSection common;
    common.sectionKind = ffui::kUiaHomeSectionCommonLocations;
    common.itemCount = 2;
    common.firstGlobalIndex = 3;
    common.headerY = 240.0f;
    surface.homeSections.push_back(common);
    for (int i = 0; i < 2; ++i) {
        fftest::FakeUiaSurface::FakeHomeItem item;
        item.sectionKind = ffui::kUiaHomeSectionCommonLocations;
        item.indexInSection = i;
        item.primary = i == 0 ? L"Downloads" : L"Unavailable Drive";
        item.secondary = i == 0 ? L"C:\\Users\\Test\\Downloads" : L"";
        item.value = item.secondary;
        item.enabled = i == 0;
        item.y = 300.0f + static_cast<float>(i) * 50.0f;
        surface.homeItems.push_back(item);
    }

    // Quick actions.
    fftest::FakeUiaSurface::FakeHomeSection actions;
    actions.sectionKind = ffui::kUiaHomeSectionQuickActions;
    actions.itemCount = 4;
    actions.firstGlobalIndex = 5;
    actions.headerY = 440.0f;
    surface.homeSections.push_back(actions);
    for (int i = 0; i < 4; ++i) {
        fftest::FakeUiaSurface::FakeHomeItem item;
        item.sectionKind = ffui::kUiaHomeSectionQuickActions;
        item.indexInSection = i;
        item.primary = L"Action" + std::to_wstring(i);
        item.value = L"command." + std::to_wstring(i);
        item.y = 500.0f + static_cast<float>(i) * 50.0f;
        surface.homeItems.push_back(item);
    }

    surface.homeFocusTarget = ffui::kUiaHomeFocusItem;
    surface.homeFocusIndex = 1; // the first Recent folder item
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

void TestHomeHierarchy() {
    auto surface = MakeHomeSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto home = FirstChild(pane.Get());
    Check(home != nullptr, "Home pane exposes the Home group");
    Check(GetName(home.Get()) == L"Home", "Home group name");
    Check(GetControlType(home.Get()) == UIA_GroupControlTypeId, "Home group is a Group");

    // Four sections in layout order: Pinned, Recent folders, Common
    // locations, Quick actions.
    auto section = FirstChild(home.Get());
    Check(section != nullptr && GetName(section.Get()) == L"Pinned",
          "first section is Pinned");
    section = NextSibling(section.Get());
    Check(section != nullptr && GetName(section.Get()) == L"Recent folders",
          "second section is Recent folders");
    section = NextSibling(section.Get());
    Check(section != nullptr && GetName(section.Get()) == L"Common locations",
          "third section is Common locations");
    section = NextSibling(section.Get());
    Check(section != nullptr && GetName(section.Get()) == L"Quick actions",
          "fourth section is Quick actions");
    Check(NextSibling(section.Get()) == nullptr, "no section beyond Quick actions");

    // Sections are headings: not focusable, no Invoke.
    Check(!GetBoolProperty(section.Get(), UIA_IsKeyboardFocusablePropertyId),
          "section headings are not focus stops");

    // Recent folders: Clear button precedes the items (header above items).
    auto recent = FirstChild(home.Get());
    recent = NextSibling(recent.Get());
    auto clear = FirstChild(recent.Get());
    Check(clear != nullptr, "Recent folders section has a first child");
    Check(GetControlType(clear.Get()) == UIA_ButtonControlTypeId,
          "Clear affordance is a Button");
    Check(GetName(clear.Get()) == L"Clear recent folders", "Clear button name");
    auto firstItem = NextSibling(clear.Get());
    Check(firstItem != nullptr && GetName(firstItem.Get()) == L"C:\\Recent0",
          "first Recent folder item follows the Clear button");
    Check(GetControlType(firstItem.Get()) == UIA_ListItemControlTypeId,
          "Recent folder item is a ListItem");
    Check(GetName(NextSibling(firstItem.Get()).Get()) == L"C:\\Recent1",
          "second Recent folder item");

    // Quick actions are Buttons.
    auto actions = FirstChild(home.Get());
    actions = NextSibling(actions.Get());  // Recent folders
    actions = NextSibling(actions.Get());  // Common locations
    actions = NextSibling(actions.Get());  // Quick actions
    auto action = FirstChild(actions.Get());
    Check(action != nullptr && GetControlType(action.Get()) == UIA_ButtonControlTypeId,
          "Quick action is a Button");
}

void TestDisabledKnownFolder() {
    auto surface = MakeHomeSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    // Navigate to the Common locations section's second item (unavailable).
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto home = FirstChild(pane.Get());
    auto section = FirstChild(home.Get());
    section = NextSibling(section.Get()); // Recent folders
    section = NextSibling(section.Get()); // Common locations
    auto item = FirstChild(section.Get());   // Downloads (available)
    item = NextSibling(item.Get());          // Unavailable Drive (disabled)
    Check(item != nullptr && GetName(item.Get()) == L"Unavailable Drive",
          "unavailable known folder is present in the tree");
    Check(!GetBoolProperty(item.Get(), UIA_IsEnabledPropertyId),
          "unavailable known folder is IsEnabled=false");
    Check(!GetBoolProperty(item.Get(), UIA_IsKeyboardFocusablePropertyId),
          "unavailable known folder is not focusable");

    // No Invoke pattern on the disabled item (GetPatternProvider is the
    // authoritative pattern surface; QI alone is not gated on state).
    auto simple = fftest::ToSimple(item.Get());
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    Check(simple != nullptr &&
              SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)) &&
              pattern == nullptr,
          "disabled item exposes no Invoke pattern");

    // SetFocus on the disabled item fails and never moves the real focus.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    Check(SUCCEEDED(item.As(&fragment)), "disabled item is a fragment");
    Check(FAILED(fragment->SetFocus()), "SetFocus on a disabled item fails");
    Check(surface.homeFocusIndex == 1, "disabled SetFocus never moved the focus");
}

void TestHomeFocusParity() {
    auto surface = MakeHomeSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    // The focused Home item reports HasKeyboardFocus.
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto home = FirstChild(pane.Get());
    auto section = FirstChild(home.Get());
    section = NextSibling(section.Get()); // Recent folders
    auto clear = FirstChild(section.Get());
    auto focusedItem = NextSibling(clear.Get()); // C:\Recent0 (global 1)
    Check(GetBoolProperty(focusedItem.Get(), UIA_HasKeyboardFocusPropertyId),
          "focused Home item reports HasKeyboardFocus");

    // UIA SetFocus on another item moves the real typed focus.
    auto otherItem = NextSibling(focusedItem.Get()); // C:\Recent1 (global 2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    Check(SUCCEEDED(otherItem.As(&fragment)), "item is a fragment");
    Check(SUCCEEDED(fragment->SetFocus()), "SetFocus on a Home item succeeds");
    Check(surface.homeFocusTarget == ffui::kUiaHomeFocusItem && surface.homeFocusIndex == 2,
          "SetFocus moved the real Home focus to global index 2");

    // Focus parity: arrow-key/Enter activation routes through the same action.
    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    Check(SUCCEEDED(focusedItem->QueryInterface(IID_IInvokeProvider, &invoke)),
          "focusable Home item exposes Invoke");
    Check(SUCCEEDED(invoke->Invoke()), "Home item Invoke succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"home-activate:0:1:0") != surface.actions.end(),
          "Home Invoke routes through the real activation path (Enter parity)");
}

void TestHomeClearInvoke() {
    auto surface = MakeHomeSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto home = FirstChild(pane.Get());
    auto section = FirstChild(home.Get());
    section = NextSibling(section.Get()); // Recent folders
    auto clear = FirstChild(section.Get());
    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    Check(SUCCEEDED(clear->QueryInterface(IID_IInvokeProvider, &invoke)),
          "Clear button exposes Invoke");
    Check(SUCCEEDED(invoke->Invoke()), "Clear button Invoke succeeds");
    Check(std::find(surface.actions.begin(), surface.actions.end(),
                    L"home-clear:0:0") != surface.actions.end(),
          "Clear Invoke routes the ClearRecentFolders activation");

    // SetFocus on the Clear stop.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    Check(SUCCEEDED(clear.As(&fragment)), "Clear button is a fragment");
    Check(SUCCEEDED(fragment->SetFocus()), "SetFocus on the Clear stop succeeds");
    Check(surface.homeFocusTarget == ffui::kUiaHomeFocusClearRecentFolders,
          "SetFocus moved the typed focus to the Clear stop");
}

void TestHomeStaleAfterRefresh() {
    auto surface = MakeHomeSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto home = FirstChild(pane.Get());
    auto section = FirstChild(home.Get());
    section = NextSibling(section.Get()); // Recent folders
    auto clear = FirstChild(section.Get());
    auto item = NextSibling(clear.Get()); // C:\Recent0
    Microsoft::WRL::ComPtr<IInvokeProvider> oldInvoke;
    Check(SUCCEEDED(item->QueryInterface(IID_IInvokeProvider, &oldInvoke)),
          "pre-refresh item exposes Invoke");

    // Home data refreshes: the same global index now holds a different value.
    surface.homeItems[1].primary = L"C:\\Other";
    surface.homeItems[1].value = L"C:\\Other";

    // The old element must fail cleanly, never retargeting the new value.
    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(item.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "stale Home item fails cleanly after refresh");
    VariantClear(&value);
    Check(FAILED(oldInvoke->Invoke()), "stale Home item cannot be invoked");

    // The new occupant resolves with its own identity.
    auto fresh = NextSibling(clear.Get());
    Check(GetName(fresh.Get()) == L"C:\\Other", "new occupant is exposed under its own identity");
}

} // namespace

int main() {
    TestHomeHierarchy();
    TestDisabledKnownFolder();
    TestHomeFocusParity();
    TestHomeClearInvoke();
    TestHomeStaleAfterRefresh();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffhome_uia_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffhome_uia_tests: all checks passed\n");
    return 0;
}
