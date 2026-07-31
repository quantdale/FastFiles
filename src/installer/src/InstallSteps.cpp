#include "InstallSteps.h"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <shlobj.h>

#include <cstdio>
#include <iterator>
#include <string>

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

    CreateDirectoryW(installDir.c_str(), nullptr);

    const bool isUpgrade = ServiceIsRegistered();

    if (!StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kIndexSvcExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kEngineExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, L"FastFiles.exe")) {
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
        if (!ffsetup::ReapplyIndexServiceSecurity(clientGroupSid->Get()).success) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to reapply service security on upgrade\n");
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

    StartIndexService();
    return 0;
}

int RunUninstall() {
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

    return 0;
}

} // namespace ffinstaller
