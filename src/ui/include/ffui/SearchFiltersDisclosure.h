#pragma once

// Search Filters disclosure identity and expand/collapse mapping.
// Consumed by the HWND UIA overlay (SearchButtonUiaProvider) and headless
// tests so AutomationId / expanded-state decisions stay off the window
// procedure. Mirrors SearchTabOrder.h.

namespace ffui {

inline constexpr wchar_t kSearchFiltersAutomationId[] = L"Search.Filters";
inline constexpr int kSearchFiltersControlId = 7110;

enum class SearchFiltersDisclosureState {
    Collapsed = 0,
    Expanded = 1,
};

constexpr SearchFiltersDisclosureState ToSearchFiltersDisclosureState(bool expanded) {
    return expanded ? SearchFiltersDisclosureState::Expanded
                    : SearchFiltersDisclosureState::Collapsed;
}

// Stable AutomationId for the Filters disclosure. Other search owner-draw
// buttons keep the decimal Win32 control id so existing UIA lookups continue
// to match native HWND identity.
constexpr const wchar_t* SearchButtonAutomationId(int controlId) {
    return controlId == kSearchFiltersControlId ? kSearchFiltersAutomationId : nullptr;
}

}  // namespace ffui
