#include "ffui/SelectionDiagnostics.h"

#include <windows.h>

#include <iostream>
#include <string>

namespace {

std::wstring g_response;

LRESULT CALLBACK ProbeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data != nullptr && data->dwData == ffui::kSelectionDiagnosticsResponse &&
            data->lpData != nullptr && data->cbData >= sizeof(wchar_t) &&
            data->cbData % sizeof(wchar_t) == 0) {
            const auto* text = static_cast<const wchar_t*>(data->lpData);
            const size_t characterCount = data->cbData / sizeof(wchar_t);
            g_response.assign(text, characterCount - (text[characterCount - 1] == L'\0' ? 1 : 0));
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int PrintError(const wchar_t* operation) {
    std::wcerr << operation << L" failed: " << GetLastError() << L"\n";
    return 1;
}

} // namespace

int wmain() {
    const HWND mainWindow = FindWindowW(L"FastFilesMainWindow", nullptr);
    if (mainWindow == nullptr) return PrintError(L"FindWindowW");

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t kProbeClassName[] = L"FastFilesSelectionRuntimeProbe";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ProbeWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kProbeClassName;
    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return PrintError(L"RegisterClassW");
    }

    const HWND probeWindow = CreateWindowExW(0, kProbeClassName, nullptr, 0, 0, 0, 0, 0,
                                             HWND_MESSAGE, nullptr, instance, nullptr);
    if (probeWindow == nullptr) return PrintError(L"CreateWindowExW");

    COPYDATASTRUCT request{};
    request.dwData = ffui::kSelectionDiagnosticsQuery;
    DWORD_PTR sendResult = 0;
    const LRESULT sent = SendMessageTimeoutW(mainWindow, WM_COPYDATA,
                                             reinterpret_cast<WPARAM>(probeWindow),
                                             reinterpret_cast<LPARAM>(&request),
                                             SMTO_ABORTIFHUNG, 5000, &sendResult);
    const DWORD sendError = sent != 0 ? ERROR_SUCCESS : GetLastError();
    DestroyWindow(probeWindow);
    UnregisterClassW(kProbeClassName, instance);
    if (sent == 0) {
        SetLastError(sendError);
        return PrintError(L"SendMessageTimeoutW");
    }
    if (g_response.empty()) {
        std::wcerr << L"selection diagnostics response was empty\n";
        return 1;
    }
    std::wcout << g_response;
    return 0;
}
