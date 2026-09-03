// Dialog focus return test: headless coverage for the dialog close
// focus seam (ffui::DialogClosedFocusTarget, consumed by SettingsDialog,
// PromptForLeafName, and ConflictDialog).
//
// When secondary windows and dialogs hide or destroy themselves, keyboard
// focus must be restored to the logical shell owner window (the main window
// that routes WM_KEYDOWN into ColumnView). If the owner is destroyed or null,
// focus target must resolve to null, never to a dead HWND handle.

#include "ffui/DialogFocusReturn.h"

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
    HWND owner = MakeProbeWindow(L"fftest_dialog_owner");
    HWND dialog = MakeProbeWindow(L"fftest_dialog_child");
    Check(owner != nullptr && dialog != nullptr, "probe owner/dialog windows exist");
    if (owner == nullptr || dialog == nullptr) {
        if (owner != nullptr) DestroyWindow(owner);
        if (dialog != nullptr) DestroyWindow(dialog);
        return;
    }
    Check(ffui::DialogClosedFocusTarget(owner, dialog) == owner,
          "live shell owner receives focus when dialog closes");
    DestroyWindow(owner);
    DestroyWindow(dialog);
}

void TestDeadOwnerReturnsNull() {
    HWND owner = MakeProbeWindow(L"fftest_dialog_dead_owner");
    HWND dialog = MakeProbeWindow(L"fftest_dialog_child2");
    Check(owner != nullptr && dialog != nullptr, "fallback probe windows exist");
    if (owner == nullptr || dialog == nullptr) {
        if (owner != nullptr) DestroyWindow(owner);
        if (dialog != nullptr) DestroyWindow(dialog);
        return;
    }
    DestroyWindow(owner);
    Check(ffui::DialogClosedFocusTarget(owner, dialog) == nullptr,
          "destroyed owner resolves to null, never a dead handle");
    Check(ffui::DialogClosedFocusTarget(nullptr, dialog) == nullptr,
          "null owner resolves to null");
    DestroyWindow(dialog);
}

} // namespace

int main() {
    TestLiveOwnerWins();
    TestDeadOwnerReturnsNull();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
