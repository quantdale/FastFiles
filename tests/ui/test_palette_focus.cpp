// Command-palette focus readiness test: headless coverage for the
// final-local-certification-closure FC-001 focus-race repair
// (ffui::PaletteEditOwnsFocus / ffui::PaletteFocusReady, consumed by
// CommandPalette::Show/EnsureEditFocus/EditHasFocus).
//
// Iteration-22 root cause: Show() issued one unchecked SetFocus(edit_) at a
// SWP_NOACTIVATE-shown child, so text entry could race ahead of focus
// acquisition. The durable contract is that text may be entered only when the
// edit exists, is enabled and visible, and is the verified focus owner.
// These tests pin that predicate matrix plus handle-identity semantics using
// real HWND probes without touching foreground focus (non-interactive safe).

#include "ffui/PaletteFocus.h"

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

void TestOwnsFocusIdentity() {
    HWND edit = MakeProbeWindow(L"fftest_palette_edit");
    HWND other = MakeProbeWindow(L"fftest_palette_other");
    Check(edit != nullptr && other != nullptr, "probe edit/other windows exist");
    if (edit == nullptr || other == nullptr) {
        if (edit != nullptr) DestroyWindow(edit);
        if (other != nullptr) DestroyWindow(other);
        return;
    }
    Check(ffui::PaletteEditOwnsFocus(edit, edit), "focused edit owns focus");
    Check(!ffui::PaletteEditOwnsFocus(edit, other), "focus on another window is not edit focus");
    Check(!ffui::PaletteEditOwnsFocus(edit, nullptr), "null focus is not edit focus");
    Check(!ffui::PaletteEditOwnsFocus(nullptr, edit), "null edit never owns focus");
    Check(!ffui::PaletteEditOwnsFocus(nullptr, nullptr), "null edit with null focus is not ready");
    DestroyWindow(edit);
    DestroyWindow(other);
}

void TestReadyMatrix() {
    Check(ffui::PaletteFocusReady(true, true, true, true), "all conditions present means ready");
    Check(!ffui::PaletteFocusReady(false, true, true, true), "missing edit is not ready");
    Check(!ffui::PaletteFocusReady(true, false, true, true), "disabled edit is not ready");
    Check(!ffui::PaletteFocusReady(true, true, false, true), "hidden edit is not ready");
    Check(!ffui::PaletteFocusReady(true, true, true, false),
          "edit without verified focus is not ready (iteration-22 case)");
    Check(!ffui::PaletteFocusReady(false, false, false, false), "nothing present is not ready");
}

} // namespace

int main() {
    TestOwnsFocusIdentity();
    TestReadyMatrix();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
