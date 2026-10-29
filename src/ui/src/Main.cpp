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
#include "Gallery.h"
#include "WindowShell.h"
#include "ShutdownDiagnostics.h"

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int showCommand) {
    // Gallery is debug-only: refuse in release even if the flag is present.
    if (ffui::IsGalleryRequested() && !ffui::IsGalleryAllowed()) {
        std::fwprintf(stderr, L"FastFiles: --gallery is only available in debug builds.\n");
        return 1;
    }

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

    // original-vision-journey-recovery 4.8: shell COM owners must die first,
    // including the Initialize failure path.
    struct OleLifetime {
        ~OleLifetime() {
            ffui::AppendShutdownPhase(L"ole-uninitialize", L"begin");
            OleUninitialize();
            ffui::AppendShutdownPhase(L"ole-uninitialize", L"end");
        }
    } oleLifetime;
    int exitCode = 1;
    {
        ffui::WindowShell shell;
        if (!shell.Initialize(instance, showCommand)) {
            MessageBoxW(nullptr, L"FastFiles could not initialize its window or renderer.\n\n"
                                L"Check the diagnostics log under %LOCALAPPDATA%\\FastFiles\\logs.",
                        L"FastFiles startup failed", MB_OK | MB_ICONERROR);
            ffui::AppendShutdownPhase(L"shell-destruction", L"begin-initialize-failed");
        } else {
            exitCode = shell.RunMessageLoop();
            ffui::AppendShutdownPhase(L"shell-destruction", L"begin");
        }
    }

    ffui::AppendShutdownPhase(L"shell-destruction", L"end");
    return exitCode;
}
