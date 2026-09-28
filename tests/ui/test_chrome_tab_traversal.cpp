// retry-ownership-and-focus-escape-closure D4: headless
// coverage for the navigation-chrome Tab-traversal seam
// (`ffui::IsChromeTabTraversalEligible`, `ffui::AppendChromeTabStop`,
// `ffui::NextChromeTabTarget`), consumed by
// `NavigationChrome::HandleTabNavigation`.
//
// The reviewed defect is a chrome-only focus cycle: the old traversal wrapped
// from the last chrome control back to the first (and reverse), so a keyboard
// user could never Tab back to the shell content surface. The shell is not a
// dialog and never calls IsDialogMessage, and Column View/Home are
// custom-rendered on the shell owner, so the boundary must explicitly target
// that owner. These tests pin entry, adjacency, both escape boundaries, the
// filtered candidate rules (disabled Back/Forward/Up, hidden overflow), and
// the eligibility rules (address editing is never stolen).

#include "ffui/ChromeTabTraversal.h"

#include <windows.h>

#include "../TestSupport.h"

using namespace fftest;

namespace {

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND MakeProbeWindow(const wchar_t* className, DWORD style) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ProbeWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, className, className, style, 0, 0, 64, 64, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

// A visible, enabled chrome control stand-in.
HWND MakeControl(const wchar_t* className, DWORD extraStyle = 0) {
    return MakeProbeWindow(className, WS_POPUP | WS_VISIBLE | extraStyle);
}

void TestEntryFromOwnerEntersAtTheNearEnd() {
    const HWND first = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x101));
    const HWND middle = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x102));
    const HWND last = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x103));
    const std::vector<HWND> focusable{first, middle, last};
    const HWND owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x104));

    const auto forward = ffui::NextChromeTabTarget(focusable, owner, owner, /*reverse=*/false);
    Check(forward.consume && forward.target == first,
          "forward Tab from the shell owner enters chrome at the first control");
    const auto reverse = ffui::NextChromeTabTarget(focusable, owner, owner, /*reverse=*/true);
    Check(reverse.consume && reverse.target == last,
          "reverse Tab from the shell owner enters chrome at the last control");
    const auto fromNowhere = ffui::NextChromeTabTarget(focusable, nullptr, owner, /*reverse=*/false);
    Check(fromNowhere.consume && fromNowhere.target == first,
          "a non-chrome focus enters chrome at the first control");
}

void TestAdjacentControlsMoveOneStep() {
    const HWND first = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x201));
    const HWND middle = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x202));
    const HWND last = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x203));
    const std::vector<HWND> focusable{first, middle, last};
    const HWND owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x204));

    const auto forward = ffui::NextChromeTabTarget(focusable, first, owner, /*reverse=*/false);
    Check(forward.consume && forward.target == middle,
          "forward Tab moves to the adjacent control, not the boundary");
    const auto reverse = ffui::NextChromeTabTarget(focusable, last, owner, /*reverse=*/true);
    Check(reverse.consume && reverse.target == middle,
          "reverse Tab moves to the adjacent control, not the boundary");
}

void TestBoundariesEscapeToTheShellOwner() {
    const HWND first = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x301));
    const HWND last = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x302));
    const std::vector<HWND> focusable{first, last};
    HWND owner = MakeProbeWindow(L"fftest_tab_owner", WS_POPUP);
    Check(owner != nullptr, "the traversal probe owner exists");
    if (owner == nullptr) return;

    const auto forward = ffui::NextChromeTabTarget(focusable, last, owner, /*reverse=*/false);
    Check(forward.consume && forward.target == owner,
          "forward Tab on the last chrome control returns focus to the shell owner");
    Check(forward.target != first,
          "forward Tab on the last chrome control does not wrap to the first control");
    const auto reverse = ffui::NextChromeTabTarget(focusable, first, owner, /*reverse=*/true);
    Check(reverse.consume && reverse.target == owner,
          "reverse Tab on the first chrome control returns focus to the shell owner");
    Check(reverse.target != last,
          "reverse Tab on the first chrome control does not wrap to the last control");
    DestroyWindow(owner);
}

void TestDeadOwnerDoesNotTrapTheKey() {
    const HWND first = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x401));
    const HWND last = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x402));
    const std::vector<HWND> focusable{first, last};

    const auto nullOwner = ffui::NextChromeTabTarget(focusable, last, nullptr, /*reverse=*/false);
    Check(!nullOwner.consume, "a missing shell owner cannot consume the boundary Tab");

    HWND deadOwner = MakeProbeWindow(L"fftest_tab_dead_owner", WS_POPUP);
    Check(deadOwner != nullptr, "the dead-owner probe window exists");
    if (deadOwner != nullptr) {
        DestroyWindow(deadOwner);
    }
    const auto destroyedOwner = ffui::NextChromeTabTarget(focusable, first, deadOwner, /*reverse=*/true);
    Check(!destroyedOwner.consume, "a destroyed shell owner cannot consume the boundary Tab");
}

void TestEmptyChromeDoesNotConsume() {
    const std::vector<HWND> empty;
    HWND owner = MakeProbeWindow(L"fftest_tab_empty_owner", WS_POPUP);
    Check(owner != nullptr, "the empty-chrome probe owner exists");
    if (owner == nullptr) return;
    const auto decision = ffui::NextChromeTabTarget(empty, owner, owner, /*reverse=*/false);
    Check(!decision.consume && decision.target == nullptr,
          "an empty chrome traversal has no Tab stop and does not consume");
    DestroyWindow(owner);
}

void TestFilterSkipsDisabledAndHiddenControls() {
    HWND visible = MakeControl(L"fftest_tab_visible");
    HWND hidden = MakeProbeWindow(L"fftest_tab_hidden", WS_POPUP);
    HWND disabled = MakeControl(L"fftest_tab_disabled", WS_DISABLED);
    Check(visible != nullptr && hidden != nullptr && disabled != nullptr,
          "the filtered-control probe windows exist");
    if (visible == nullptr || hidden == nullptr || disabled == nullptr) {
        if (visible) DestroyWindow(visible);
        if (hidden) DestroyWindow(hidden);
        if (disabled) DestroyWindow(disabled);
        return;
    }

    std::vector<HWND> focusable;
    ffui::AppendChromeTabStop(focusable, visible);
    ffui::AppendChromeTabStop(focusable, hidden);
    ffui::AppendChromeTabStop(focusable, disabled);
    ffui::AppendChromeTabStop(focusable, nullptr);
    Check(focusable.size() == 1 && focusable[0] == visible,
          "only shown and enabled controls become Tab stops (disabled Back/Forward/Up and hidden overflow skipped)");

    DestroyWindow(visible);
    DestroyWindow(hidden);
    DestroyWindow(disabled);
}

void TestOverflowPresentAndAbsentStillEscapes() {
    HWND owner = MakeProbeWindow(L"fftest_tab_overflow_owner", WS_POPUP);
    HWND search = MakeControl(L"fftest_tab_search");
    HWND settings = MakeControl(L"fftest_tab_settings");
    HWND newTab = MakeControl(L"fftest_tab_newtab");
    HWND overflow = MakeControl(L"fftest_tab_overflow");
    HWND hiddenOverflow = MakeProbeWindow(L"fftest_tab_overflow_hidden", WS_POPUP);
    Check(owner != nullptr && search != nullptr && settings != nullptr && newTab != nullptr &&
              overflow != nullptr && hiddenOverflow != nullptr,
          "the overflow probe windows exist");
    if (owner == nullptr || search == nullptr || settings == nullptr || newTab == nullptr ||
        overflow == nullptr || hiddenOverflow == nullptr) {
        if (owner) DestroyWindow(owner);
        if (search) DestroyWindow(search);
        if (settings) DestroyWindow(settings);
        if (newTab) DestroyWindow(newTab);
        if (overflow) DestroyWindow(overflow);
        if (hiddenOverflow) DestroyWindow(hiddenOverflow);
        return;
    }

    std::vector<HWND> withOverflow;
    ffui::AppendChromeTabStop(withOverflow, search);
    ffui::AppendChromeTabStop(withOverflow, settings);
    ffui::AppendChromeTabStop(withOverflow, newTab);
    ffui::AppendChromeTabStop(withOverflow, overflow);
    const auto forwardBoundary =
        ffui::NextChromeTabTarget(withOverflow, withOverflow.back(), owner, /*reverse=*/false);
    Check(forwardBoundary.consume && forwardBoundary.target == owner,
          "with overflow shown, forward Tab from it escapes to the shell owner");

    std::vector<HWND> withoutOverflow;
    ffui::AppendChromeTabStop(withoutOverflow, search);
    ffui::AppendChromeTabStop(withoutOverflow, settings);
    ffui::AppendChromeTabStop(withoutOverflow, newTab);
    ffui::AppendChromeTabStop(withoutOverflow, hiddenOverflow); // hidden: not a stop
    Check(withoutOverflow.size() == 3,
          "a hidden overflow affordance is not a Tab stop");
    const auto reverseBoundary =
        ffui::NextChromeTabTarget(withoutOverflow, withoutOverflow.front(), owner, /*reverse=*/true);
    Check(reverseBoundary.consume && reverseBoundary.target == owner,
          "without overflow, reverse Tab from the first control escapes to the shell owner");

    DestroyWindow(owner);
    DestroyWindow(search);
    DestroyWindow(settings);
    DestroyWindow(newTab);
    DestroyWindow(overflow);
    DestroyWindow(hiddenOverflow);
}

void TestSeveralTabsKeepOneBoundaryEach() {
    HWND owner = MakeProbeWindow(L"fftest_tab_many_owner", WS_POPUP);
    std::vector<HWND> controls;
    std::vector<HWND> focusable;
    for (int index = 0; index < 8; ++index) {
        controls.push_back(MakeControl(L"fftest_tab_many"));
    }
    for (HWND control : controls) {
        ffui::AppendChromeTabStop(focusable, control);
    }
    Check(owner != nullptr && focusable.size() == controls.size(),
          "several visible tab controls all enter the traversal");
    if (owner == nullptr || focusable.empty()) {
        if (owner) DestroyWindow(owner);
        for (HWND control : controls) DestroyWindow(control);
        return;
    }

    const auto forward = ffui::NextChromeTabTarget(focusable, focusable.back(), owner, /*reverse=*/false);
    Check(forward.consume && forward.target == owner,
          "forward Tab from the last of several visible controls escapes to the shell owner");
    const auto reverse = ffui::NextChromeTabTarget(focusable, focusable.front(), owner, /*reverse=*/true);
    Check(reverse.consume && reverse.target == owner,
          "reverse Tab from the first of several visible controls escapes to the shell owner");

    DestroyWindow(owner);
    for (HWND control : controls) DestroyWindow(control);
}

void TestEligibilityNeverStealsAddressEditing() {
    Check(!ffui::IsChromeTabTraversalEligible(/*chromeWindowValid=*/true, /*ownerValid=*/true, /*editing=*/true,
                                              /*focusedIsShellSurface=*/false, /*focusedIsChromeControl=*/true,
                                              /*focusedIsAddressEdit=*/true),
          "an open address edit is never stolen by chrome traversal");
    Check(!ffui::IsChromeTabTraversalEligible(true, true, false, false, true, true),
          "focus on the address edit is never moved by chrome traversal");
    Check(ffui::IsChromeTabTraversalEligible(true, true, false, true, false, false),
          "the shell content surface is eligible for chrome entry");
    Check(ffui::IsChromeTabTraversalEligible(true, true, false, false, true, false),
          "a chrome control is eligible for chrome traversal");
    Check(!ffui::IsChromeTabTraversalEligible(true, true, false, false, false, false),
          "focus outside the shell and chrome is not hijacked");
    Check(!ffui::IsChromeTabTraversalEligible(false, true, false, true, false, false),
          "a missing chrome window is not eligible");
    Check(!ffui::IsChromeTabTraversalEligible(true, false, false, true, false, false),
          "a missing shell owner is not eligible");
}

} // namespace

int main() {
    TestEntryFromOwnerEntersAtTheNearEnd();
    TestAdjacentControlsMoveOneStep();
    TestBoundariesEscapeToTheShellOwner();
    TestDeadOwnerDoesNotTrapTheKey();
    TestEmptyChromeDoesNotConsume();
    TestFilterSkipsDisabledAndHiddenControls();
    TestOverflowPresentAndAbsentStillEscapes();
    TestSeveralTabsKeepOneBoundaryEach();
    TestEligibilityNeverStealsAddressEditing();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
