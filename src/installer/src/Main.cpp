// FastFilesSetup: installs/uninstalls FastFiles (tasks 6.1-6.4).
//
// A small native installer rather than an MSI/WiX package -- the build
// system choice for the product itself is CMake (task 1.1), and this
// keeps the installer in the same toolchain rather than introducing a
// separate packaging dependency. See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <shellapi.h>

#include <iterator>
#include <string>

#include "InstallSteps.h"

#pragma comment(lib, "shell32.lib")

namespace {

bool IsRunningElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

// Single UAC prompt (design.md Migration Plan): relaunches itself
// elevated with the same arguments, waits, and propagates the exit code.
int RelaunchElevated(const std::wstring& arguments) {
    wchar_t ownPath[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, ownPath, static_cast<DWORD>(std::size(ownPath)));

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = ownPath;
    info.lpParameters = arguments.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info) || info.hProcess == nullptr) {
        return 1;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    return static_cast<int>(exitCode);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    const bool uninstall = argc > 1 && wcscmp(argv[1], L"/uninstall") == 0;

    if (!IsRunningElevated()) {
        return RelaunchElevated(uninstall ? L"/uninstall" : L"/install");
    }

    return uninstall ? ffinstaller::RunUninstall() : ffinstaller::RunInstall();
}
