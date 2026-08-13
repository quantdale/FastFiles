// FastFilesSetup: installs/uninstalls FastFiles (tasks 6.1-6.4).
//
// A small native installer rather than an MSI/WiX package -- the build
// system choice for the product itself is CMake (task 1.1), and this
// keeps the installer in the same toolchain rather than introducing a
// separate packaging dependency. See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "InstallSteps.h"
#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/PackageTrust.h"

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

std::wstring GetOwnPath() {
    wchar_t ownPath[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, ownPath, static_cast<DWORD>(std::size(ownPath)));
    return length == 0 ? std::wstring() : std::wstring(ownPath, length);
}

bool IsInstalledUninstaller(const std::wstring& ownPath) {
    wchar_t programFiles[MAX_PATH * 4];
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", programFiles, static_cast<DWORD>(std::size(programFiles)));
    if (length == 0 || length >= std::size(programFiles)) {
        return false;
    }
    const std::wstring expected = std::wstring(programFiles, length) + L"\\FastFiles\\FastFilesSetup.exe";
    return _wcsicmp(ownPath.c_str(), expected.c_str()) == 0;
}

bool ScheduleSelfDeletion(const std::wstring& ownPath) {
    HANDLE file = CreateFileW(ownPath.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        FILE_DISPOSITION_INFO_EX disposition{};
        disposition.Flags = FILE_DISPOSITION_FLAG_DELETE
            | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
            | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
        if (SetFileInformationByHandle(file, FileDispositionInfoEx,
                                       &disposition, sizeof(disposition))) {
            CloseHandle(file);
            return true;
        }
        CloseHandle(file);
    }
    // The fallback is a system-managed deletion at reboot. No executable is
    // copied into a user-writable directory and no helper process is spawned.
    return MoveFileExW(ownPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

bool VerifyApprovedSignatureOnly(const std::wstring& path) {
    const auto actual = ffsetup::VerifyAuthenticodeAndGetThumbprintAtPath(path);
    if (!actual) return false;
    const auto approved = ffsetup::ApprovedPackageSigners();
    return std::any_of(approved.begin(), approved.end(), [&](const ffsetup::Thumbprint& expected) {
        return !ffsetup::IsPlaceholderThumbprint(expected) &&
               ffsetup::ConstantTimeThumbprintEqual(*actual, expected);
    });
}

bool VerifyInstallerBeforeElevation(const std::wstring& ownPath, bool requirePackageManifest) {
    const std::wstring packageRoot = DirectoryOf(ownPath);
    const auto manifest = ffsetup::ReadAuthenticatedPackageManifest(
        packageRoot, ffsetup::ApprovedPackageSigners());
    if (manifest) {
        const auto* expected = ffsetup::FindPackageManifestEntry(*manifest, L"FastFilesSetup.exe");
        if (expected != nullptr &&
            ffsetup::VerifyPackageFileAtPath(ownPath, *expected, ffsetup::ApprovedPackageSigners())) {
            return true;
        }
        if (requirePackageManifest) return false;
    } else if (requirePackageManifest) {
        return false;
    }
    // Uninstall must remain recoverable if an old or damaged installation has
    // lost its metadata manifest. The running installer is still required to
    // be an approved signed image; RunUninstall separately refuses reparse
    // install roots and never follows child junctions.
    return !requirePackageManifest && VerifyApprovedSignatureOnly(ownPath);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    const bool uninstall = argc > 1 && wcscmp(argv[1], L"/uninstall") == 0;
    const bool uninstallFinal = argc > 1 && wcscmp(argv[1], L"/uninstall-final") == 0;

    const std::wstring ownPath = GetOwnPath();
    if (!IsRunningElevated()) {
        if (!VerifyInstallerBeforeElevation(ownPath, !uninstall && !uninstallFinal)) {
            return 1;
        }
        return RelaunchElevated((uninstall || uninstallFinal) ? L"/uninstall" : L"/install");
    }
    if (!VerifyInstallerBeforeElevation(ownPath, !uninstall && !uninstallFinal)) {
        return 1;
    }

    if (uninstall || uninstallFinal) {
        const int result = ffinstaller::RunUninstall(ownPath);
        if (result == 0 && IsInstalledUninstaller(ownPath) && !ScheduleSelfDeletion(ownPath)) {
            return 1;
        }
        return result;
    }
    return ffinstaller::RunInstall();
}
