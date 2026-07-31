// FastFiles: the UI shell (Column View browsing).
//
// Direct2D/DirectComposition window shell, Engine control-pipe client, and
// Finder-style multi-column navigation reading through the engine's
// degraded-mode directory listing (design.md D5). See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <objbase.h>

#include "WindowShell.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ffui::WindowShell shell;
    if (!shell.Initialize(instance, showCommand)) {
        CoUninitialize();
        return 1;
    }

    const int exitCode = shell.RunMessageLoop();
    CoUninitialize();
    return exitCode;
}
