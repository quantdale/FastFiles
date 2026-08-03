#include "InstallSteps.h"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <shlobj.h>
#include <werapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "ffprotocol/Diagnostics.h"
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

// --- Transactional upgrade (resolve-raw-volume-privilege-insufficiency
// task 3.2 / spec "Failed upgrade restores the prior service") ------------
//
// Before any binary is replaced, the current binaries and the service's
// current account configuration are snapshotted into <installDir>\Backup.
// If the post-install startup verification fails, RestoreServiceState puts
// the old binaries and account configuration back and restarts the service,
// so an upgrade never leaves the machine with a broken half-installed
// service.

constexpr wchar_t kBackupDirName[] = L"Backup";
constexpr wchar_t kBackupStateFileName[] = L"install-state.txt";
constexpr wchar_t kInstallerExeName[] = L"FastFilesSetup.exe";
constexpr wchar_t kUiExeName[] = L"FastFiles.exe";

std::wstring BackupDirectory(const std::wstring& installDir) {
    return installDir + L"\\" + kBackupDirName;
}

// Snapshot the installed binaries + SCM account configuration before an
// upgrade touches anything. Fails closed: an unreadable prior state aborts
// the upgrade while the machine is still untouched.
bool BackupServiceState(const std::wstring& installDir,
                        const ffsetup::ServiceAccountOptions& priorAccount) {
    const std::wstring backupDir = BackupDirectory(installDir);
    if (!CreateDirectoryW(backupDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    const wchar_t* binaries[] = {
        ffsetup::kIndexSvcExeName, ffsetup::kEngineExeName, kUiExeName, kInstallerExeName,
    };
    for (const wchar_t* exeName : binaries) {
        if (!CopyFileW((installDir + L"\\" + exeName).c_str(), (backupDir + L"\\" + exeName).c_str(), FALSE)) {
            return false;
        }
    }

    std::wofstream stateFile(backupDir + L"\\" + kBackupStateFileName, std::ios::trunc);
    if (!stateFile) {
        return false;
    }
    stateFile << L"type=" << static_cast<int>(priorAccount.type) << L"\n";
    stateFile << L"name=" << priorAccount.userName << L"\n";
    return static_cast<bool>(stateFile);
}

std::optional<ffsetup::ServiceAccountOptions> ReadBackupServiceAccount(const std::wstring& installDir) {
    std::wifstream stateFile(BackupDirectory(installDir) + L"\\" + kBackupStateFileName);
    if (!stateFile) {
        return std::nullopt;
    }
    ffsetup::ServiceAccountOptions options;
    std::wstring line;
    bool sawType = false;
    while (std::getline(stateFile, line)) {
        if (line.rfind(L"type=", 0) == 0 && line.size() > 5) {
            try {
                options.type = static_cast<ffsetup::ServiceAccountType>(std::stoi(line.substr(5)));
            } catch (const std::exception&) {
                return std::nullopt;
            }
            sawType = true;
        } else if (line.rfind(L"name=", 0) == 0 && line.size() > 5) {
            options.userName = line.substr(5);
        }
    }
    if (!sawType || options.type == ffsetup::ServiceAccountType::NamedUser) {
        return std::nullopt;
    }
    return options;
}

// Restores the prior known-good state captured by BackupServiceState:
// removes the (possibly half-configured) new registration, restores the old
// binaries and account configuration, and restarts the service. Returns true
// when the prior state is running again.
bool RestoreServiceState(const std::wstring& installDir, PSID clientGroupSid) {
    std::fwprintf(stderr, L"FastFilesSetup: rolling back upgrade to prior known-good state\n");
    const std::optional<ffsetup::ServiceAccountOptions> priorAccount = ReadBackupServiceAccount(installDir);
    if (!priorAccount) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback aborted -- prior account state unreadable\n");
        return false;
    }

    if (!ffsetup::UnregisterIndexService().success) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback failed to remove the new service registration\n");
        return false;
    }

    const wchar_t* binaries[] = {
        ffsetup::kIndexSvcExeName, ffsetup::kEngineExeName, kUiExeName, kInstallerExeName,
    };
    for (const wchar_t* exeName : binaries) {
        if (!CopyFileW((BackupDirectory(installDir) + L"\\" + exeName).c_str(),
                       (installDir + L"\\" + exeName).c_str(), FALSE)) {
            std::fwprintf(stderr, L"FastFilesSetup: rollback failed to restore %ls\n", exeName);
            return false;
        }
    }

    const std::wstring servicePath = installDir + L"\\" + ffsetup::kIndexSvcExeName;
    if (!ffsetup::RegisterIndexService(servicePath, clientGroupSid, *priorAccount).success) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback failed to re-register the prior service\n");
        return false;
    }
    StartIndexService();
    return true;
}

bool ServiceIsRunning() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }
    SC_HANDLE service = OpenServiceW(scm, ffsetup::kServiceName, SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS status{};
    const bool running = QueryServiceStatus(service, &status) != FALSE && status.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return running;
}

bool WaitForServiceRunning(DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    do {
        if (ServiceIsRunning()) {
            return true;
        }
        Sleep(200);
    } while (GetTickCount64() < deadline);
    return ServiceIsRunning();
}

// Startup verification (task 3.2): the service must reach RUNNING and stay
// up past a settle window (a crash-on-start binary would trigger SCM's
// failure actions and be back down here), and the service's own startup-token
// diagnostic -- written after the fresh token is in place (task 2.3) -- must
// not report a missing/disabled backup privilege. A missing diagnostic line
// is tolerated (the log can be unavailable), but an explicit privilege
// failure is not.
bool VerifyServiceStartedFresh() {
    const std::wstring diagPath = ffprotocol::DiagnosticLogPath();
    std::error_code fileError;
    const ULONGLONG logBytesBefore = diagPath.empty() ? 0
        : static_cast<ULONGLONG>(std::filesystem::file_size(std::filesystem::path(diagPath), fileError));

    StartIndexService();
    if (!WaitForServiceRunning(30000)) {
        std::fwprintf(stderr, L"FastFilesSetup: verification failed -- service did not reach RUNNING\n");
        return false;
    }
    Sleep(2500); // settle: a crash-on-start binary restarts and is down again
    if (!ServiceIsRunning()) {
        std::fwprintf(stderr, L"FastFilesSetup: verification failed -- service did not stay running\n");
        return false;
    }

    if (!diagPath.empty() && logBytesBefore != 0) {
        std::wifstream log(diagPath, std::ios::binary);
        log.seekg(static_cast<std::streamoff>(logBytesBefore));
        std::wstring line;
        while (std::getline(log, line)) {
            if (line.find(L"state=startup-token") != std::wstring::npos) {
                if (line.find(L"privilegeEnabled=0") != std::wstring::npos) {
                    std::fwprintf(stderr, L"FastFilesSetup: verification failed -- fresh token lacks SeBackupPrivilege\n");
                    return false;
                }
                std::fwprintf(stderr, L"FastFilesSetup: verification passed -- fresh token has the raw-volume privilege\n");
                return true;
            }
        }
        std::fwprintf(stderr, L"FastFilesSetup: startup-token diagnostic not found -- proceeding (log unavailable)\n");
    }
    return true;
}

void RemoveBackupState(const std::wstring& installDir) {
    DeleteKnownTree(BackupDirectory(installDir));
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

    // Selected production model (evidence/matrix-execution-and-selection.md):
    // the constrained privileged broker -- FastFilesIndexSvc under LocalSystem
    // with SeBackupPrivilege and the existing narrow closed command surface.
    const ffsetup::ServiceAccountOptions brokerOptions{ffsetup::ServiceAccountType::LocalSystem};

    // Task 2.3: an identity/right change must force a fresh service start
    // before verification -- stop now so the re-registration below starts the
    // service on a freshly granted token. Task 3.2: snapshot the prior
    // binaries and account configuration before any replacement so a failed
    // verification can roll back.
    std::optional<ffsetup::ServiceAccountOptions> priorAccount;
    if (isUpgrade) {
        if (!StopIndexService()) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to stop FastFilesIndexSvc for upgrade\n");
            return 1;
        }
        priorAccount = ffsetup::QueryIndexServiceAccount();
        if (!priorAccount) {
            std::fwprintf(stderr, L"FastFilesSetup: could not read the prior service account configuration -- aborting\n");
            return 1;
        }
        if (!BackupServiceState(installDir, *priorAccount)) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to back up the prior service state -- aborting upgrade\n");
            return 1;
        }
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

    auto rollback = [&]() {
        if (isUpgrade) {
            RestoreServiceState(installDir, clientGroupSid->Get());
        } else {
            ffsetup::UnregisterIndexService();
        }
    };

    if (!StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kIndexSvcExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kEngineExeName) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, L"FastFiles.exe") ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, L"FastFilesSetup.exe")) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to stage/install binaries\n");
        rollback();
        return 1;
    }

    const std::wstring servicePath = installDir + L"\\" + ffsetup::kIndexSvcExeName;
    if (isUpgrade) {
        // Full re-registration: moves the service to the selected broker
        // identity (the fresh token is what task 2.3 verifies) and reapplies
        // the descriptor/failure actions (task 6.3) in one transaction.
        if (!ffsetup::UnregisterIndexService().success) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to remove the prior service registration for upgrade\n");
            rollback();
            return 1;
        }
    }
    if (!ffsetup::RegisterIndexService(servicePath, clientGroupSid->Get(), brokerOptions).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to register FastFilesIndexSvc\n");
        rollback();
        return 1;
    }

    if (!ffsetup::ApplyInstallDirectorySecurity(installDir).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to ACL the install directory\n");
        rollback();
        return 1;
    }

    const std::wstring enginePath = installDir + L"\\" + ffsetup::kEngineExeName;
    if (!ffsetup::RegisterEngineScheduledTask(enginePath).success) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to register the FastFilesEngine scheduled task\n");
        rollback();
        return 1;
    }

    const std::wstring installerPath = installDir + L"\\FastFilesSetup.exe";
    if (!RegisterUninstallMetadata(installerPath, installDir)) {
        std::fwprintf(stderr, L"FastFilesSetup: failed to register uninstall metadata\n");
        rollback();
        return 1;
    }

    // Task 3.2 / spec "Failed upgrade restores the prior service": the
    // post-install startup verification decides whether the upgrade stays.
    if (!VerifyServiceStartedFresh()) {
        rollback();
        return 1;
    }

    RemoveBackupState(installDir);
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
