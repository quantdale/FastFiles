#include "QuickActions.h"

#include <filesystem>
#include <shellapi.h>

namespace ffui {
namespace {
constexpr wchar_t kNamePromptClass[] = L"FastFilesNamePrompt";
struct NamePromptState {
    HWND owner = nullptr;
    HWND edit = nullptr;
    std::wstring initialValue;
    std::optional<std::wstring> result;
    bool done = false;
};

LRESULT CALLBACK NamePromptProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<NamePromptState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<NamePromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_CREATE: {
            CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
                            12, 14, 60, 22, window, nullptr, nullptr, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->initialValue.c_str(),
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          72, 10, 310, 25, window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            220, 50, 75, 26, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                            307, 50, 75, 26, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                const int length = GetWindowTextLengthW(state->edit);
                std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(state->edit, value.data(), length + 1);
                value.resize(static_cast<size_t>(length));
                state->result = std::move(value);
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            state->done = true;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    struct ClipboardCloser { ~ClipboardCloser() { CloseClipboard(); } } closer;
    if (!EmptyClipboard()) return false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) return false;
    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        return false;
    }
    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

bool CopyPathsToClipboard(HWND owner, const std::vector<std::wstring>& paths) {
    std::wstring text;
    for (const auto& path : paths) {
        if (!text.empty()) text += L"\r\n";
        text += path;
    }
    return !paths.empty() && CopyTextToClipboard(owner, text);
}

std::vector<std::wstring> PathsRelativeTo(const std::vector<std::wstring>& paths,
                                          const std::wstring& base, bool& usedAbsoluteFallback) {
    usedAbsoluteFallback = false;
    std::vector<std::wstring> result;
    for (const auto& path : paths) {
        std::error_code error;
        auto relative = std::filesystem::relative(path, base, error);
        if (error || relative.empty()) {
            usedAbsoluteFallback = true;
            result.push_back(path);
        } else {
            result.push_back(relative.wstring());
        }
    }
    return result;
}

bool OpenWithDefaultApplication(HWND owner, const std::wstring& path) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

bool ShowOpenWithPicker(HWND owner, const std::wstring& path) {
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.hwnd = owner;
    execute.lpVerb = L"openas";
    execute.lpFile = path.c_str();
    execute.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&execute) != FALSE;
}

ProcessLaunchSpec BuildTerminalLaunchSpec(const std::wstring& targetDirectory,
                                          const std::wstring& preferredShell) {
    return {preferredShell, preferredShell, targetDirectory};
}

bool LaunchTerminalHere(HWND owner, const std::wstring& targetDirectory) {
    for (const wchar_t* executable : {L"powershell.exe", L"cmd.exe"}) {
        ProcessLaunchSpec spec = BuildTerminalLaunchSpec(targetDirectory, executable);
        std::vector<wchar_t> commandLine(spec.commandLine.begin(), spec.commandLine.end());
        commandLine.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                           nullptr, spec.currentDirectory.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }
    }
    MessageBoxW(owner, L"Neither PowerShell nor the Windows Command Processor could be launched.",
                L"Open terminal", MB_OK | MB_ICONERROR);
    return false;
}

std::optional<std::wstring> PromptForLeafName(HWND owner, const std::wstring& title,
                                              const std::wstring& initialValue) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = NamePromptProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kNamePromptClass;
    RegisterClassExW(&windowClass);

    NamePromptState state{owner, nullptr, initialValue, std::nullopt, false};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    HWND prompt = CreateWindowExW(WS_EX_DLGMODALFRAME, kNamePromptClass, title.c_str(),
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  ownerRect.left + 80, ownerRect.top + 80, 410, 120,
                                  owner, nullptr, instance, &state);
    if (prompt == nullptr) return std::nullopt;
    EnableWindow(owner, FALSE);
    ShowWindow(prompt, SW_SHOW);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.hwnd == state.edit) {
            if (message.wParam == VK_RETURN) {
                SendMessageW(prompt, WM_COMMAND, IDOK, 0);
                continue;
            }
            if (message.wParam == VK_ESCAPE) {
                SendMessageW(prompt, WM_COMMAND, IDCANCEL, 0);
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.result;
}

} // namespace ffui
