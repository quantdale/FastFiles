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
#include <cstring>
#include <string>
#include <vector>

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

int RelayInstalledUninstall(const std::wstring& ownPath) {
    wchar_t tempDirectory[MAX_PATH * 4];
    const DWORD tempLength = GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory);
    if (tempLength == 0 || tempLength >= std::size(tempDirectory)) {
        return 1;
    }
    const std::wstring relayPath = std::wstring(tempDirectory, tempLength)
        + L"FastFilesSetup-Uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!CopyFileW(ownPath.c_str(), relayPath.c_str(), FALSE)) {
        return 1;
    }

    std::wstring commandLine = L"\"" + relayPath + L"\" /uninstall-final";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(relayPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        DeleteFileW(relayPath.c_str());
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

void DeleteRelayExecutable(const std::wstring& ownPath) {
    wchar_t commandProcessor[MAX_PATH * 4];
    const DWORD commandProcessorLength = GetEnvironmentVariableW(
        L"ComSpec", commandProcessor, static_cast<DWORD>(std::size(commandProcessor)));
    if (commandProcessorLength > 0 && commandProcessorLength < std::size(commandProcessor)) {
        // A system-owned helper waits for this relay process to release its
        // image section, then removes the exact fixed-pattern path. No task,
        // service, or custom helper executable is left behind.
        std::wstring commandLine = L"\"" + std::wstring(commandProcessor, commandProcessorLength)
            + L"\" /d /c ping 127.0.0.1 -n 2 >nul & del /f /q \""
            + ownPath + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (CreateProcessW(commandProcessor, commandLine.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }

    HANDLE file = CreateFileW(ownPath.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        FILE_DISPOSITION_INFO_EX disposition{};
        disposition.Flags = FILE_DISPOSITION_FLAG_DELETE
            | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
            | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
        if (SetFileInformationByHandle(file, FileDispositionInfoEx,
                                       &disposition, sizeof(disposition))) {
            CloseHandle(file);
            return;
        }
        CloseHandle(file);
    }
    // NTFS permits renaming the executable's default data stream while its
    // image section is mapped. Moving that stream to an ADS releases the
    // path so a normal delete disposition can remove the directory entry.
    constexpr wchar_t kDeleteStream[] = L":FastFilesDelete";
    file = CreateFileW(ownPath.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const DWORD streamBytes = static_cast<DWORD>((std::size(kDeleteStream) - 1) * sizeof(wchar_t));
        std::vector<BYTE> renameBuffer(sizeof(FILE_RENAME_INFO) + streamBytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(renameBuffer.data());
        rename->ReplaceIfExists = TRUE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = streamBytes;
        memcpy(rename->FileName, kDeleteStream, streamBytes);
        const bool renamed = SetFileInformationByHandle(
            file, FileRenameInfo, rename, static_cast<DWORD>(renameBuffer.size())) != FALSE;
        CloseHandle(file);
        if (renamed) {
            file = CreateFileW(ownPath.c_str(), DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                FILE_DISPOSITION_INFO disposition{};
                disposition.DeleteFile = TRUE;
                const bool deleted = SetFileInformationByHandle(
                    file, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
                CloseHandle(file);
                if (deleted) {
                    return;
                }
            }
        }
    }
    // Compatibility fallback for filesystems/Windows builds without POSIX
    // delete semantics. The install itself is already gone; only this relay
    // copy remains pending deletion.
    MoveFileExW(ownPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    const bool uninstall = argc > 1 && wcscmp(argv[1], L"/uninstall") == 0;
    const bool uninstallFinal = argc > 1 && wcscmp(argv[1], L"/uninstall-final") == 0;

    if (!IsRunningElevated()) {
        return RelaunchElevated((uninstall || uninstallFinal) ? L"/uninstall" : L"/install");
    }

    const std::wstring ownPath = GetOwnPath();
    if (uninstall && IsInstalledUninstaller(ownPath)) {
        return RelayInstalledUninstall(ownPath);
    }
    if (uninstall || uninstallFinal) {
        const int result = ffinstaller::RunUninstall();
        if (uninstallFinal) {
            DeleteRelayExecutable(ownPath);
        }
        return result;
    }
    return ffinstaller::RunInstall();
}
