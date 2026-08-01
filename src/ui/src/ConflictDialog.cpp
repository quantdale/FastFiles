#include "ConflictDialog.h"

#include <filesystem>

namespace ffui {

namespace {

constexpr wchar_t kConflictWindowClass[] = L"FastFilesConflictDialog";
constexpr int kReplace = 1001;
constexpr int kSkip = 1002;
constexpr int kKeepBoth = 1003;
constexpr int kApplyAll = 1004;

struct DialogState {
    ConflictDecision result;
    HWND applyAll = nullptr;
};

LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_COMMAND && state != nullptr) {
        ConflictChoice choice = ConflictChoice::Cancel;
        switch (LOWORD(wParam)) {
            case kReplace: choice = ConflictChoice::Replace; break;
            case kSkip: choice = ConflictChoice::Skip; break;
            case kKeepBoth: choice = ConflictChoice::KeepBoth; break;
            case IDCANCEL: break;
            default: return DefWindowProcW(window, message, wParam, lParam);
        }
        state->result = {choice, SendMessageW(state->applyAll, BM_GETCHECK, 0, 0) == BST_CHECKED};
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE && state != nullptr) {
        state->result = {ConflictChoice::Cancel, false};
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ApplyFont(HWND window) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

} // namespace

ConflictDecision ShowConflictDialog(HWND owner, const std::wstring& source, const std::wstring& destination) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DialogProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kConflictWindowClass;
        registered = RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return {};

    DialogState state{};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 620;
    const int height = 245;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kConflictWindowClass, L"FastFiles — File name conflict",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                  owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr) return {};
    const std::wstring sourceName = std::filesystem::path(source).filename().wstring();
    const std::wstring text = L"The destination already contains ‘" + sourceName + L"’.\r\n\r\n" + destination;
    HWND label = CreateWindowExW(0, L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE,
                                 22, 20, 570, 72, dialog, nullptr, nullptr, nullptr);
    state.applyAll = CreateWindowExW(0, L"BUTTON", L"Apply this choice to all remaining conflicts",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 22, 100, 400, 26,
                                     dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyAll)), nullptr, nullptr);
    HWND replace = CreateWindowExW(0, L"BUTTON", L"Replace", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   22, 150, 120, 32, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplace)), nullptr, nullptr);
    HWND skip = CreateWindowExW(0, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                152, 150, 120, 32, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSkip)), nullptr, nullptr);
    HWND keep = CreateWindowExW(0, L"BUTTON", L"Keep Both", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                282, 150, 120, 32, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kKeepBoth)), nullptr, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  472, 150, 120, 32, dialog, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
    for (HWND control : {dialog, label, state.applyAll, replace, skip, keep, cancel}) ApplyFont(control);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    SetFocus(keep);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.result;
}

} // namespace ffui
