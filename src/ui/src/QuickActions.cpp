#include "QuickActions.h"

#include "UITheme.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <shellapi.h>
#include <shlobj.h>
#include <utility>

namespace ffui {
namespace {
constexpr wchar_t kNamePromptClass[] = L"FastFilesNamePrompt";
// ui-v3 secondary surfaces: the name prompt joins the shared theme system —
// themed background/edit/static colors from GetUiTheme(gUiDarkTheme), product
// typography, and an immersive dark title bar — instead of raw system chrome.
struct NamePromptState {
    HWND owner = nullptr;
    HWND edit = nullptr;
    std::wstring initialValue;
    std::optional<std::wstring> result;
    bool done = false;
    HBRUSH themeBrush = nullptr;
    HFONT bodyFont = nullptr;
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
            auto scaled = [](float dip) { return static_cast<int>(ffui::UiScale(dip)); };
            const HFONT font = state->bodyFont;
            HWND nameStatic = CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
                                              scaled(12), scaled(14), scaled(60), scaled(22),
                                              window, nullptr, nullptr, nullptr);
            if (font != nullptr) SendMessageW(nameStatic, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            state->edit = CreateWindowExW(0, L"EDIT", state->initialValue.c_str(),
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          scaled(72), scaled(10), scaled(310), scaled(25), window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)), nullptr, nullptr);
            HWND okButton = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                            scaled(220), scaled(50), scaled(75), scaled(26), window,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr);
            HWND cancelButton = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                                                scaled(307), scaled(50), scaled(75), scaled(26), window,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr);
            if (font != nullptr) {
                SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
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
        // ui-v3 secondary surfaces: themed background and control colors so
        // the prompt never appears as a raw system-light island in dark mode.
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const UiTheme theme = GetUiTheme(gUiDarkTheme);
            SetTextColor(dc, ToColorRef(theme.text));
            SetBkColor(dc, ToColorRef(theme.background));
            return reinterpret_cast<LRESULT>(state->themeBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const UiTheme theme = GetUiTheme(gUiDarkTheme);
            SetTextColor(dc, ToColorRef(theme.searchText));
            SetBkColor(dc, ToColorRef(theme.surfaceElevated));
            // Self-healing one-entry cache: recreated whenever the elevated
            // surface color changes (theme flip).
            static HBRUSH cachedEditBrush = nullptr;
            static COLORREF cachedColor = 0;
            const COLORREF elevated = ToColorRef(theme.surfaceElevated);
            if (cachedEditBrush == nullptr || cachedColor != elevated) {
                if (cachedEditBrush != nullptr) {
                    DeleteObject(cachedEditBrush);
                }
                cachedEditBrush = CreateSolidBrush(elevated);
                cachedColor = elevated;
            }
            return reinterpret_cast<LRESULT>(cachedEditBrush);
        }
        case WM_DESTROY:
            state->done = true;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

// OpenClipboard fails transiently whenever another window (clipboard manager,
// Office, RDP client) holds the clipboard open. Retry briefly before giving up
// so routine contention does not silently drop a copy.
bool TryOpenClipboard(HWND owner) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) return true;
        Sleep(10);
    }
    return false;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!TryOpenClipboard(owner)) return false;
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

bool CopyFilesToClipboard(HWND owner, const std::vector<std::wstring>& paths, bool cut) {
    if (paths.empty()) return false;
    size_t characterCount = 1; // The double-null terminator.
    for (const auto& path : paths) {
        if (path.empty() || characterCount > std::numeric_limits<size_t>::max() / sizeof(wchar_t) ||
            path.size() > (std::numeric_limits<size_t>::max() / sizeof(wchar_t)) - characterCount - 1) {
            return false;
        }
        characterCount += path.size() + 1;
    }
    if (characterCount > (std::numeric_limits<size_t>::max() - sizeof(DROPFILES)) / sizeof(wchar_t)) {
        return false;
    }
    const size_t bytes = sizeof(DROPFILES) + characterCount * sizeof(wchar_t);
    if (!TryOpenClipboard(owner)) return false;
    struct ClipboardCloser { ~ClipboardCloser() { CloseClipboard(); } } closer;
    if (!EmptyClipboard()) return false;
    HGLOBAL memory = GlobalAlloc(GHND, bytes);
    if (memory == nullptr) return false;
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    if (drop == nullptr) {
        GlobalFree(memory);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    wchar_t* destination = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(drop) + sizeof(DROPFILES));
    for (const auto& path : paths) {
        std::memcpy(destination, path.c_str(), path.size() * sizeof(wchar_t));
        destination += path.size();
        *destination++ = L'\0';
    }
    *destination = L'\0';
    GlobalUnlock(memory);
    if (SetClipboardData(CF_HDROP, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }

    const UINT effectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (effectFormat != 0) {
        HGLOBAL effectMemory = GlobalAlloc(GHND, sizeof(DWORD));
        if (effectMemory != nullptr) {
            auto* effect = static_cast<DWORD*>(GlobalLock(effectMemory));
            if (effect != nullptr) {
                *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
                GlobalUnlock(effectMemory);
                if (SetClipboardData(effectFormat, effectMemory) == nullptr) {
                    GlobalFree(effectMemory);
                }
            } else {
                GlobalFree(effectMemory);
            }
        }
    }
    return true;
}

std::vector<std::wstring> ReadFilesFromClipboard(HWND owner, bool& cut) {
    cut = false;
    std::vector<std::wstring> paths;
    if (!TryOpenClipboard(owner)) return paths;
    struct ClipboardCloser { ~ClipboardCloser() { CloseClipboard(); } } closer;
    auto* drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop == nullptr) return paths;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
    paths.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        if (length == 0) continue;
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        if (DragQueryFileW(drop, index, path.data(), length + 1) != 0) {
            path.resize(length);
            paths.push_back(std::move(path));
        }
    }
    const UINT effectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (effectFormat != 0) {
        if (auto* effect = static_cast<const DWORD*>(GetClipboardData(effectFormat)); effect != nullptr) {
            cut = (*effect & DROPEFFECT_MOVE) != 0;
        }
    }
    return paths;
}

bool HasFilesOnClipboard(HWND owner) {
    if (!TryOpenClipboard(owner)) return false;
    struct ClipboardCloser { ~ClipboardCloser() { CloseClipboard(); } } closer;
    return GetClipboardData(CF_HDROP) != nullptr;
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

    NamePromptState state{owner, nullptr, initialValue, std::nullopt, false, nullptr, nullptr};
    // ui-v3 secondary surfaces: themed resources live for the dialog's
    // lifetime and are freed after the modal loop.
    state.themeBrush = CreateSolidBrush(ToColorRef(GetUiTheme(gUiDarkTheme).background));
    const int bodyHeight = -static_cast<int>(UiScale(UiMetrics::kFontSizeBody));
    state.bodyFont = CreateFontW(bodyHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    auto scaled = [](float dip) { return static_cast<int>(UiScale(dip)); };
    HWND prompt = CreateWindowExW(WS_EX_DLGMODALFRAME, kNamePromptClass, title.c_str(),
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  ownerRect.left + scaled(80), ownerRect.top + scaled(80),
                                  scaled(410), scaled(120),
                                  owner, nullptr, instance, &state);
    if (prompt == nullptr) {
        if (state.themeBrush != nullptr) DeleteObject(state.themeBrush);
        if (state.bodyFont != nullptr) DeleteObject(state.bodyFont);
        return std::nullopt;
    }
    // ui-v3 secondary surfaces: immersive dark title bar in dark theme.
    if (HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll")) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto dwmSetAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        if (dwmSetAttribute != nullptr) {
            const BOOL useDark = gUiDarkTheme ? TRUE : FALSE;
            dwmSetAttribute(prompt, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
        }
    }
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
    if (state.themeBrush != nullptr) DeleteObject(state.themeBrush);
    if (state.bodyFont != nullptr) DeleteObject(state.bodyFont);
    return state.result;
}

} // namespace ffui
