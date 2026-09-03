// keyboard-focus-forensics FF-VIS-FOCUS-001: headless coverage for the
// address-bar close focus seam (`ffui::AddressEditClosedFocusTarget`,
// consumed by `NavigationChrome::CommitAddressEdit` / `EndAddressEdit`).
//
// Live forensics (GetGUIThreadInfo + UIA FocusedElement) proved that after a
// successful Ctrl+L commit, keyboard focus sat on the `FastFilesNavigationChrome`
// child instead of the shell owner, so {DOWN} never reached ColumnView and the
// live `browse-keyboard-focus` capture was pixel-identical (duplicate guard
// FAIL). The seam pins the repair decision with real HWNDs: a live owner wins,
// a dead/null owner falls back to the chrome window, never to the hidden edit.

#include "ffui/AddressBarFocus.h"

#include <windows.h>

#include "../TestSupport.h"

using namespace fftest;

namespace {

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND MakeProbeWindow(const wchar_t* title) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ProbeWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = title;
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, title, title, WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

void TestLiveOwnerWins() {
    HWND owner = MakeProbeWindow(L"fftest_address_owner");
    HWND chrome = MakeProbeWindow(L"fftest_address_chrome");
    Check(owner != nullptr && chrome != nullptr, "probe owner/chrome windows exist");
    if (owner == nullptr || chrome == nullptr) {
        if (owner != nullptr) DestroyWindow(owner);
        if (chrome != nullptr) DestroyWindow(chrome);
        return;
    }
    Check(ffui::AddressEditClosedFocusTarget(owner, chrome) == owner,
          "live shell owner receives focus when the address edit closes");
    DestroyWindow(owner);
    DestroyWindow(chrome);
}

void TestDeadOwnerFallsBackToChrome() {
    HWND owner = MakeProbeWindow(L"fftest_address_dead_owner");
    HWND chrome = MakeProbeWindow(L"fftest_address_chrome2");
    Check(owner != nullptr && chrome != nullptr, "fallback probe windows exist");
    if (owner == nullptr || chrome == nullptr) {
        if (owner != nullptr) DestroyWindow(owner);
        if (chrome != nullptr) DestroyWindow(chrome);
        return;
    }
    DestroyWindow(owner);
    Check(ffui::AddressEditClosedFocusTarget(owner, chrome) == chrome,
          "destroyed owner falls back to the chrome window, never a dead handle");
    Check(ffui::AddressEditClosedFocusTarget(nullptr, chrome) == chrome,
          "null owner falls back to the chrome window");
    DestroyWindow(chrome);
}

} // namespace

int main() {
    TestLiveOwnerWins();
    TestDeadOwnerFallsBackToChrome();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
