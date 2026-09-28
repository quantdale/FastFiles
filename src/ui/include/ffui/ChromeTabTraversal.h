#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <vector>

// retry-ownership-and-focus-escape-closure D4: pure,
// headless seam for navigation-chrome Tab traversal. The shell is not a
// dialog and its message loop never calls IsDialogMessage, so the chrome
// must move focus itself. The reviewed defect is the chrome-only cycle: the
// old traversal wrapped from the last control back to the first (and from
// the first to the last), trapping keyboard users in the chrome. Column View
// and Home are custom-rendered on the shell owner, so returning false at a
// boundary does not restore content focus either — the boundary must
// explicitly target that owner.
//
// The HWND decisions live here so they can be unit tested with real windows
// without compiling the chrome (see tests/ui/test_chrome_tab_traversal.cpp);
// NavigationChrome owns the single call site.

namespace ffui {

// The next Tab stop and whether the caller should consume the key.
struct ChromeTabTarget {
    HWND target = nullptr;
    bool consume = false;
};

// The chrome traversal only runs for the shell owner or a chrome control and
// never while the address editor owns keyboard focus. These mirror the early
// returns NavigationChrome::HandleTabNavigation applies before it builds the
// candidate list.
inline bool IsChromeTabTraversalEligible(bool chromeWindowValid, bool ownerValid, bool editing,
                                         bool focusedIsShellSurface, bool focusedIsChromeControl,
                                         bool focusedIsAddressEdit) noexcept {
    if (!chromeWindowValid || !ownerValid || editing) {
        return false;
    }
    if (!focusedIsShellSurface && !focusedIsChromeControl) {
        return false;
    }
    return !focusedIsAddressEdit;
}

// Appends a chrome control as a Tab stop only while it is shown and enabled.
// Disabled Back/Forward/Up controls and a hidden tab-overflow affordance must
// stay out of the traversal, exactly like the chrome's own visibility rules.
inline void AppendChromeTabStop(std::vector<HWND>& focusable, HWND control) {
    if (control != nullptr && IsWindowVisible(control) != FALSE && IsWindowEnabled(control) != FALSE) {
        focusable.push_back(control);
    }
}

// `focusable` is the already-filtered, in-order chrome vector. Focus on the
// owner (or on no chrome control) enters at the near end; moving past either
// end returns to the shell owner instead of wrapping inside chrome; otherwise
// the adjacent control is the target. A dead or missing owner yields no
// consume, so a lost owner can never trap the key.
inline ChromeTabTarget NextChromeTabTarget(const std::vector<HWND>& focusable, HWND focused, HWND owner,
                                           bool reverse) noexcept {
    if (focusable.empty()) {
        return {};
    }
    const auto found = std::find(focusable.begin(), focusable.end(), focused);
    if (found == focusable.end()) {
        const HWND target = reverse ? focusable.back() : focusable.front();
        return {target, target != nullptr};
    }
    const size_t index = static_cast<size_t>(std::distance(focusable.begin(), found));
    const bool atBoundary = reverse ? index == 0 : index + 1 == focusable.size();
    if (atBoundary) {
        const HWND target = (owner != nullptr && IsWindow(owner) != FALSE) ? owner : nullptr;
        return {target, target != nullptr};
    }
    const HWND target = reverse ? focusable[index - 1] : focusable[index + 1];
    return {target, target != nullptr};
}

} // namespace ffui
