#include "InstallSteps.h"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <shlobj.h>
#include <werapi.h>

#include <cstdio>
#include <iterator>
#include <string>
#include <utility>

#include "ffsetup/GroupSetup.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/InstallDirAcl.h"
#include "ffsetup/ScheduledTaskRegistration.h"
#include "ffsetup/SecurityDescriptors.h"
#include "ffsetup/ServiceRegistration.h"

#include "ScratchPath.h"

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "shell32.lib")

namespace ffinstaller {

namespace {

class ScratchDirectoryGuard {
public:
    explicit ScratchDirectoryGuard(std::wstring path) : path_(std::move(path)) {}
    ~ScratchDirectoryGuard() {
        const std::wstring searchPattern = path_ + L"\\*";
        WIN32_FIND_DATAW findData{};
        HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
                    DeleteFileW((path_ + L"\\" + findData.cFileName).c_str());
                }
            } while (FindNextFileW(findHandle, &findData));
            FindClose(findHandle);
        }
        RemoveDirectoryW(path_.c_str());
    }

    ScratchDirectoryGuard(const ScratchDirectoryGuard&) = delete;
    ScratchDirectoryGuard& operator=(const ScratchDirectoryGuard&) = delete;

private:
    std::wstring path_;
};

std::wstring GetSourceDirectory() {
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0) {
        return L"";
    }
    std::wstring fullPath(path, length);
    const size_t lastSlash = fullPath.find_last_of(L"\\/");
    return lastSlash == std::wstring::npos ? L"" : fullPath.substr(0, lastSlash);
}

std::wstring GetInstallDirectory() {
    PWSTR programFilesPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFilesPath))) {
        return L"";
    }
    std::wstring result(programFilesPath);
    CoTaskMemFree(programFilesPath);
    return result + L"\\FastFiles";
}

std::wstring GetCurrentUserSamName() {
    wchar_t buffer[512];
    ULONG size = static_cast<ULONG>(std::size(buffer));
    if (!GetUserNameExW(NameSamCompatible, buffer, &size)) {
        return L"";
    }
    return std::wstring(buffer, size);
}

// Task 6.2: stage each binary in a freshly verified scratch directory,
// then move it into place -- rather than copying directly into the
// install directory -- so a TOCTOU race can't substitute a different file
// at a predictable destination path mid-copy.
bool StageAndInstallBinary(const std::wstring& scratchDir, const std::wstring& sourceDir,
                           const std::wstring& installDir, const wchar_t* exeName) {
    const std::wstring sourcePath = sourceDir + L"\\" + exeName;
    const std::wstring scratchFilePath = scratchDir + L"\\" + exeName;
    const std::wstring installFilePath = installDir + L"\\" + exeName;

    if (!CopyFileW(sourcePath.c_str(), scratchFilePath.c_str(), FALSE)) {
        return false;
    }
    if (!MoveFileExW(scratchFilePath.c_str(), installFilePath.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return false;
    }
    return true;
}

bool ServiceIsRegistered() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }
    SC_HANDLE service = OpenServiceW(scm, ffsetup::kServiceName, SERVICE_QUERY_STATUS);
    const bool exists = service != nullptr;
    if (service != nullptr) {
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return exists;
}

void StartIndexService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return;
    }
    SC_HANDLE service = OpenServiceW(scm, ffsetup::kServiceName, SERVICE_START);
    if (service != nullptr) {
        StartServiceW(service, 0, nullptr);
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
}

bool StopIndexService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }
    SC_HANDLE service = OpenServiceW(scm, ffsetup::kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(scm);
        return error == ERROR_SERVICE_DOES_NOT_EXIST;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }
    if (status.dwCurrentState != SERVICE_STOPPED) {
        SERVICE_STATUS basicStatus{};
        if (status.dwCurrentState != SERVICE_STOP_PENDING
            && !ControlService(service, SERVICE_CONTROL_STOP, &basicStatus)) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }
        const ULONGLONG deadline = GetTickCount64() + 30000;
        do {
            Sleep(200);
            if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                      reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                return false;
            }
        } while (status.dwCurrentState != SERVICE_STOPPED && GetTickCount64() < deadline);
    }
    const bool stopped = status.dwCurrentState == SERVICE_STOPPED;
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return stopped;
}

bool SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool RegisterUninstallMetadata(const std::wstring& installerPath, const std::wstring& installDir) {
    HKEY key = nullptr;
    constexpr wchar_t kPath[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FastFiles";
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring uninstallCommand = L"\"" + installerPath + L"\" /uninstall";
    const bool ok = SetRegistryString(key, L"DisplayName", L"FastFiles")
        && SetRegistryString(key, L"DisplayVersion", L"0.1.0")
        && SetRegistryString(key, L"Publisher", L"FastFiles")
        && SetRegistryString(key, L"InstallLocation", installDir)
        && SetRegistryString(key, L"UninstallString", uninstallCommand)
        && SetRegistryString(key, L"QuietUninstallString", uninstallCommand);
    RegCloseKey(key);
    return ok;
}

void UnregisterUninstallMetadata() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FastFiles");
}

void DeleteKnownTree(const std::wstring& path) {
    const DWORD rootAttributes = GetFileAttributesW(path.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return;
    }
    const std::wstring searchPattern = path + L"\\*";
    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring child = path + L"\\" + findData.cFileName;
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    RemoveDirectoryW(child.c_str());
                } else {
                    DeleteKnownTree(child);
                }
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(findHandle, &findData));
        FindClose(findHandle);
    }
    RemoveDirectoryW(path.c_str());
}

void RemoveDiagnosticState() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\FastFilesIndexSvc.exe");
    WerRemoveExcludedApplication(ffsetup::kIndexSvcExeName, FALSE);

    PWSTR programDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programDataPath))) {
        const std::wstring fastFilesPath = std::wstring(programDataPath) + L"\\FastFiles";
        CoTaskMemFree(programDataPath);
        DeleteKnownTree(fastFilesPath);
    }
}

} // namespace

int RunInstall() {
    const std::wstring sourceDir = GetSourceDirectory();
    const std::wstring installDir = GetInstallDirectory();
    if (sourceDir.empty() || installDir.empty()) {
        return 1;
    }

    auto scratchDir = CreateVerifiedScratchDirectory();
    if (!scratchDir) {
        std::fwprintf(stderr, L"FastFilesSetup: could not create a verified scratch directory -- aborting\n");
        return 1;
    }
    ScratchDirectoryGuard scratchGuard(*scratchDir);

    CreateDirectoryW(installDir.c_str(), nullptr);

    const bool isUpgrade = ServiceIsRegistered();

    if (isUpgrade && !StopIndexService()) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to stop FastFilesIndexSvc for upgrade\n");
        return 1;
    }

    if (!StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kIndexSvcExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kEngineExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, L"FastFiles.exe") ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, L"FastFilesSetup.exe")) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to stage/install binaries\n");
        return 1;
    }

    const std::wstring userName = GetCurrentUserSamName();
    if (userName.empty() || !ffsetup::CreateAuthorizedClientGroupAndAddUser(userName).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to create/populate the authorized client group\n");
        return 1;
    }

    auto clientGroupSid = ffsetup::LookupAccountSid(ffsetup::kAuthorizedClientGroupName);
    if (!clientGroupSid) {
        std::fwprintf(stderr, L"FastFilesSetup: authorized client group did not resolve after creation\n");
        return 1;
    }

    const std::wstring servicePath = installDir + L"\\" + ffsetup::kIndexSvcExeName;
    if (isUpgrade) {
        // Task 6.3: reapply security on every upgrade, not just first
        // install.
        const ffsetup::SetupResult securityResult = ffsetup::ReapplyIndexServiceSecurity(clientGroupSid->Get());
        if (!securityResult.success) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to reapply service security on upgrade (error %lu)\n",
                          securityResult.errorCode);
            return 1;
        }
    } else {
        if (!ffsetup::RegisterIndexService(servicePath, clientGroupSid->Get()).success) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to register FastFilesIndexSvc\n");
            return 1;
        }
    }

    if (!ffsetup::ApplyInstallDirectorySecurity(installDir).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to ACL the install directory\n");
        return 1;
    }

    const std::wstring enginePath = installDir + L"\\" + ffsetup::kEngineExeName;
    if (!ffsetup::RegisterEngineScheduledTask(enginePath).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to register the FastFilesEngine scheduled task\n");
        return 1;
    }

    const std::wstring installerPath = installDir + L"\\FastFilesSetup.exe";
    if (!RegisterUninstallMetadata(installerPath, installDir)) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to register uninstall metadata\n");
        return 1;
    }

    StartIndexService();
    return 0;
}

int RunUninstall() {
    UnregisterUninstallMetadata();
    ffsetup::UnregisterEngineScheduledTask();
    ffsetup::UnregisterIndexService();
    ffsetup::DeleteAuthorizedClientGroup();

    const std::wstring installDir = GetInstallDirectory();
    if (!installDir.empty()) {
        const std::wstring searchPattern = installDir + L"\\*";
        WIN32_FIND_DATAW findData{};
        HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                    continue;
                }
                DeleteFileW((installDir + L"\\" + findData.cFileName).c_str());
            } while (FindNextFileW(findHandle, &findData));
            FindClose(findHandle);
        }
        RemoveDirectoryW(installDir.c_str());
    }

    RemoveDiagnosticState();

    return 0;
}

} // namespace ffinstaller
