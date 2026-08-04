// FastFiles: the UI shell (Column View browsing).
//
// Direct2D/DirectComposition window shell, Engine control-pipe client, and
// Finder-style multi-column navigation reading through the engine's
// degraded-mode directory listing (design.md D5). See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <objbase.h>

#include "WindowShell.h"

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int showCommand) {
    // Enables per-monitor DPI awareness so the Direct2D content renders crisply
    // at non-100% scaling and WM_DPICHANGED actually fires. Fall back safely on
    // older builds (< Windows 10 1703) where the API does not exist.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setDpiAwareness = reinterpret_cast<SetDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiAwareness) {
            setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    if (FAILED(OleInitialize(nullptr))) {
        return 1;
    }

    ffui::WindowShell shell;
    if (!shell.Initialize(instance, showCommand)) {
        OleUninitialize();
        return 1;
    }

    const int exitCode = shell.RunMessageLoop();
    OleUninitialize();
    return exitCode;
}
