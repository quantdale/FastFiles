// FastFiles: the UI shell (Column View browsing).
//
// Direct2D/DirectComposition window shell, Engine control-pipe client, and
// Finder-style multi-column navigation reading through the engine's
// degraded-mode directory listing (design.md D5). See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <objbase.h>
#include <cstdio>

#include "ffprotocol/Diagnostics.h"
#include "ffsetup/PinnedSignatures.h"
#include "WindowShell.h"

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int showCommand) {
#ifndef _DEBUG
    if (!ffsetup::ArePinnedSignaturesConfigured()) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED,
                                      {}, {}, L"authenticode-pins-unconfigured", ERROR_INVALID_DATA, 0});
        std::fwprintf(stderr,
                      L"FastFiles: refusing non-debug startup because Authenticode pins are not provisioned.\n");
        return 1;
    }
#endif

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
        MessageBoxW(nullptr, L"FastFiles could not initialize Windows shell integration.",
                    L"FastFiles startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }

    ffui::WindowShell shell;
    if (!shell.Initialize(instance, showCommand)) {
        MessageBoxW(nullptr, L"FastFiles could not initialize its window or renderer.\n\n"
                            L"Check the diagnostics log under %LOCALAPPDATA%\\FastFiles\\logs.",
                    L"FastFiles startup failed", MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    const int exitCode = shell.RunMessageLoop();
    OleUninitialize();
    return exitCode;
}
