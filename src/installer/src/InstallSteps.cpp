#include "InstallSteps.h"

#include <windows.h>
#include <aclapi.h>
#define SECURITY_WIN32
#include <security.h>
#include <shlobj.h>
#include <werapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ffprotocol/Diagnostics.h"
#include "ffsetup/FastFilesVersion.h"
#include "ffsetup/GroupSetup.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/InstallDirAcl.h"
#include "ffsetup/PackageTrust.h"
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
                    const std::wstring child = path_ + L"\\" + findData.cFileName;
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        // The scratch layout is intentionally flat. Remove a
                        // reparse child itself, never its target; a normal
                        // unexpected directory is left for the final
                        // RemoveDirectory call to fail closed.
                        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                            RemoveDirectoryW(child.c_str());
                        }
                    } else {
                        DeleteFileW(child.c_str());
                    }
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

constexpr wchar_t kPackageManifestFileName[] = L"FastFiles.package.manifest";
constexpr wchar_t kInstallerExeName[] = L"FastFilesSetup.exe";
constexpr wchar_t kUiExeName[] = L"FastFiles.exe";

std::vector<std::wstring> PackageBinaryNames() {
    return {ffsetup::kIndexSvcExeName, ffsetup::kEngineExeName, kUiExeName, kInstallerExeName};
}

std::vector<ffsetup::Thumbprint> ApprovedSignersForPayload(const wchar_t* exeName) {
    if (wcscmp(exeName, ffsetup::kEngineExeName) == 0) {
        if (ffsetup::IsPlaceholderThumbprint(ffsetup::kExpectedEngineSignatureThumbprint)) return {};
        return {ffsetup::kExpectedEngineSignatureThumbprint};
    }
    if (wcscmp(exeName, ffsetup::kIndexSvcExeName) == 0) {
        if (ffsetup::IsPlaceholderThumbprint(ffsetup::kExpectedIndexSvcSignatureThumbprint)) return {};
        return {ffsetup::kExpectedIndexSvcSignatureThumbprint};
    }
    return ffsetup::ApprovedPackageSigners();
}

bool ManifestsEqual(const ffsetup::PackageManifest& left, const ffsetup::PackageManifest& right) {
    if (left.schemaVersion != right.schemaVersion || left.productVersion != right.productVersion ||
        left.files.size() != right.files.size()) {
        return false;
    }
    for (const auto& expected : left.files) {
        const auto* actual = ffsetup::FindPackageManifestEntry(right, expected.fileName);
        if (actual == nullptr || actual->sizeBytes != expected.sizeBytes || actual->sha256 != expected.sha256) {
            return false;
        }
    }
    return true;
}

std::string DigestHex(const ffsetup::Sha256Digest& digest) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        result.push_back(kHex[(byte >> 4) & 0x0F]);
        result.push_back(kHex[byte & 0x0F]);
    }
    return result;
}

std::string AsciiString(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character > 0x7F) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool WriteCanonicalManifest(const std::wstring& path, const ffsetup::PackageManifest& manifest) {
    std::ostringstream content;
    content << "FASTFILES_PACKAGE_MANIFEST_V1\n";
    content << "version=" << manifest.schemaVersion << "\n";
    const std::string productVersion = AsciiString(manifest.productVersion);
    if (productVersion.empty()) return false;
    content << "productVersion=" << productVersion << "\n";
    for (const auto& entry : manifest.files) {
        const std::string name = AsciiString(entry.fileName);
        if (name.empty()) return false;
        content << "file=" << name << "|" << entry.sizeBytes << "|" << DigestHex(entry.sha256) << "\n";
    }
    const std::string bytes = content.str();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool written = true;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD remaining = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 64 * 1024));
        DWORD bytesWritten = 0;
        if (!WriteFile(file, bytes.data() + offset, remaining, &bytesWritten, nullptr) || bytesWritten == 0) {
            written = false;
            break;
        }
        offset += bytesWritten;
    }
    written = written && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!written) DeleteFileW(path.c_str());
    return written;
}

bool StageAndInstallManifest(const std::wstring& scratchDir, const std::wstring& installDir,
                             const ffsetup::PackageManifest& manifest) {
    const std::wstring stagedPath = scratchDir + L"\\" + kPackageManifestFileName;
    const std::wstring installedPath = installDir + L"\\" + kPackageManifestFileName;
    if (!WriteCanonicalManifest(stagedPath, manifest)) return false;
    const DWORD attributes = GetFileAttributesW(installedPath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    if (!MoveFileExW(stagedPath.c_str(), installedPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(stagedPath.c_str());
        return false;
    }
    const auto installedManifest = ffsetup::ReadPackageManifest(installedPath);
    if (!installedManifest || !ManifestsEqual(manifest, *installedManifest)) {
        DeleteFileW(installedPath.c_str());
        return false;
    }
    return true;
}

bool ValidatePayloadSet(const std::wstring& root, const ffsetup::PackageManifest& manifest) {
    return ffsetup::ValidateTrustedPackageSet(
        root, manifest, PackageBinaryNames(),
        [](const std::wstring& name) { return ApprovedSignersForPayload(name.c_str()); });
}

// Task 6.2 / FF-AUDIT-01: stage each binary in a freshly verified scratch
// directory, consuming an already-open source handle whose identity, hash,
// and approved Authenticode signer were checked against the package manifest.
// Promotion is same-volume only and revalidates the resulting destination.
bool StageAndInstallBinary(const std::wstring& scratchDir, const std::wstring& sourceDir,
                           const std::wstring& installDir, const wchar_t* exeName,
                           const ffsetup::PackageManifest& manifest) {
    const std::wstring scratchFilePath = scratchDir + L"\\" + exeName;
    const std::wstring installFilePath = installDir + L"\\" + exeName;

    const auto* expected = ffsetup::FindPackageManifestEntry(manifest, exeName);
    if (expected == nullptr) {
        return false;
    }
    const auto approvedSigners = ApprovedSignersForPayload(exeName);
    auto trusted = ffsetup::OpenTrustedPackageFile(sourceDir, exeName, *expected, approvedSigners);
    if (!trusted || !ffsetup::CopyTrustedPackageFile(*trusted, scratchFilePath)) {
        return false;
    }
    return ffsetup::PromoteVerifiedPackageFile(scratchFilePath, installFilePath, *expected, approvedSigners);
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
    const bool ok = SetRegistryString(key, L"DisplayName", ffsetup::kProductName)
        && SetRegistryString(key, L"DisplayVersion", ffsetup::kProductVersion)
        && SetRegistryString(key, L"Publisher", ffsetup::kPublisherName)
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

std::wstring BackupDirectory(const std::wstring& installDir) {
    return installDir + L"\\" + kBackupDirName;
}

// Snapshot the installed binaries + SCM account configuration before an
// upgrade touches anything. Fails closed: an unreadable prior state aborts
// the upgrade while the machine is still untouched.
bool BackupServiceState(const std::wstring& installDir,
                        const ffsetup::ServiceAccountOptions& priorAccount,
                        const ffsetup::PackageManifest& priorManifest) {
    if (!ValidatePayloadSet(installDir, priorManifest)) {
        std::fwprintf(stderr, L"FastFilesSetup: prior installation failed package trust validation; rollback unavailable\n");
        return false;
    }
    const std::wstring backupDir = BackupDirectory(installDir);
    if (!CreateDirectoryW(backupDir.c_str(), nullptr)) {
        // A pre-existing backup is ambiguous after an interrupted upgrade;
        // never allow it to become an implicit trusted rollback source.
        return false;
    }

    const std::wstring priorManifestPath = installDir + L"\\" + kPackageManifestFileName;
    const std::wstring backupManifestPath = backupDir + L"\\" + kPackageManifestFileName;
    if (!CopyFileW(priorManifestPath.c_str(), backupManifestPath.c_str(), TRUE)) {
        DeleteKnownTree(backupDir);
        return false;
    }
    const auto copiedManifest = ffsetup::ReadPackageManifest(backupManifestPath);
    if (!copiedManifest || !ManifestsEqual(priorManifest, *copiedManifest)) {
        DeleteKnownTree(backupDir);
        return false;
    }

    for (const auto& name : PackageBinaryNames()) {
        const auto* expected = ffsetup::FindPackageManifestEntry(priorManifest, name);
        if (expected == nullptr) {
            DeleteKnownTree(backupDir);
            return false;
        }
        const auto signers = ApprovedSignersForPayload(name.c_str());
        auto trusted = ffsetup::OpenTrustedPackageFile(installDir, name, *expected, signers);
        if (!trusted || !ffsetup::CopyTrustedPackageFile(*trusted, backupDir + L"\\" + name) ||
            !ffsetup::VerifyPackageFileAtPath(backupDir + L"\\" + name, *expected, signers)) {
            DeleteKnownTree(backupDir);
            return false;
        }
    }

    std::wofstream stateFile(backupDir + L"\\" + kBackupStateFileName, std::ios::trunc);
    if (!stateFile) {
        return false;
    }
    stateFile << L"schema=2\n";
    stateFile << L"type=" << static_cast<int>(priorAccount.type) << L"\n";
    stateFile << L"name=" << priorAccount.userName << L"\n";
    if (!stateFile) {
        DeleteKnownTree(backupDir);
        return false;
    }
    return true;
}

std::optional<ffsetup::ServiceAccountOptions> ReadBackupServiceAccount(const std::wstring& installDir) {
    std::wifstream stateFile(BackupDirectory(installDir) + L"\\" + kBackupStateFileName);
    if (!stateFile) {
        return std::nullopt;
    }
    ffsetup::ServiceAccountOptions options;
    std::wstring line;
    bool sawSchema = false;
    bool sawType = false;
    while (std::getline(stateFile, line)) {
        if (line.rfind(L"schema=", 0) == 0 && line.size() > 7) {
            try {
                sawSchema = std::stoi(line.substr(7)) == 2;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else if (line.rfind(L"type=", 0) == 0 && line.size() > 5) {
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
    if (!sawSchema || !sawType || options.type == ffsetup::ServiceAccountType::NamedUser) {
        return std::nullopt;
    }
    return options;
}

// Restores the prior known-good state captured by BackupServiceState:
// removes the (possibly half-configured) new registration, restores the old
// binaries and account configuration, and restarts the service. Returns true
// when the prior state is running again.
bool RestoreServiceState(const std::wstring& installDir, const std::wstring& scratchDir,
                         PSID clientGroupSid) {
    std::fwprintf(stderr, L"FastFilesSetup: rolling back upgrade to prior known-good state\n");
    const std::optional<ffsetup::ServiceAccountOptions> priorAccount = ReadBackupServiceAccount(installDir);
    if (!priorAccount) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback aborted -- prior account state unreadable\n");
        return false;
    }

    const auto priorManifest = ffsetup::ReadPackageManifest(
        BackupDirectory(installDir) + L"\\" + kPackageManifestFileName);
    if (!priorManifest || !ValidatePayloadSet(BackupDirectory(installDir), *priorManifest)) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback aborted -- prior payload is untrusted or incomplete\n");
        return false;
    }

    if (!ffsetup::UnregisterIndexService().success) {
        std::fwprintf(stderr, L"FastFilesSetup: rollback failed to remove the new service registration\n");
        return false;
    }

    for (const auto& name : PackageBinaryNames()) {
        const auto* expected = ffsetup::FindPackageManifestEntry(*priorManifest, name);
        if (expected == nullptr) return false;
        const auto signers = ApprovedSignersForPayload(name.c_str());
        auto trusted = ffsetup::OpenTrustedPackageFile(BackupDirectory(installDir), name, *expected, signers);
        const std::wstring stagedPath = scratchDir + L"\\rollback-" + name;
        if (!trusted || !ffsetup::CopyTrustedPackageFile(*trusted, stagedPath) ||
            !ffsetup::PromoteVerifiedPackageFile(stagedPath, installDir + L"\\" + name,
                                                 *expected, signers)) {
            std::fwprintf(stderr, L"FastFilesSetup: rollback failed to restore %ls\n", name.c_str());
            return false;
        }
    }

    const std::wstring stagedManifest = scratchDir + L"\\rollback-" + kPackageManifestFileName;
    if (!WriteCanonicalManifest(stagedManifest, *priorManifest) ||
        !MoveFileExW(stagedManifest.c_str(), (installDir + L"\\" + kPackageManifestFileName).c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(stagedManifest.c_str());
        return false;
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
// not report a missing/disabled backup privilege. The token line is read from
// the byte offset recorded before the start, so a fresh install (offset 0)
// is verified from the beginning of the log rather than skipped. A missing
// diagnostic line is tolerated (the log can be unavailable), but an explicit
// privilege failure is not.
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

    if (!diagPath.empty()) {
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

// Fresh-install rollback (workstream E): a later install step may have
// already applied the restrictive install-dir ACL (ApplyInstallDirectorySecurity
// sets a protected, non-inheriting DACL) and staged binaries. Reset the ACL so
// the parent's inherited ACEs (e.g. Program Files) propagate back, returning
// the directory to its default state before the staged binaries are removed.
void ResetInstallDirectoryAcl(const std::wstring& path) {
    ACL emptyAcl;
    if (!InitializeAcl(&emptyAcl, sizeof(emptyAcl), ACL_REVISION)) {
        return;
    }
    SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &emptyAcl, nullptr);
}

void DeleteInstalledBinaries(const std::wstring& installDir) {
    const wchar_t* binaries[] = {
        ffsetup::kIndexSvcExeName, ffsetup::kEngineExeName, kUiExeName, kInstallerExeName,
    };
    for (const wchar_t* exeName : binaries) {
        DeleteFileW((installDir + L"\\" + exeName).c_str());
    }
    DeleteFileW((installDir + L"\\" + kPackageManifestFileName).c_str());
}

} // namespace

int RunInstall() {
    const std::wstring sourceDir = GetSourceDirectory();
    const std::wstring installDir = GetInstallDirectory();
    if (sourceDir.empty() || installDir.empty()) {
        return 1;
    }

    const auto sourceManifest = ffsetup::ReadPackageManifest(sourceDir + L"\\" + kPackageManifestFileName);
    if (!sourceManifest || !ffsetup::HasExactPackageFileSet(*sourceManifest, PackageBinaryNames())) {
        std::fwprintf(stderr, L"FastFilesSetup: package manifest is missing or incomplete -- aborting\n");
        return 1;
    }

    auto scratchDir = CreateVerifiedScratchDirectory();
    if (!scratchDir) {
        std::fwprintf(stderr, L"FastFilesSetup: could not create a verified scratch directory -- aborting\n");
        return 1;
    }
    ScratchDirectoryGuard scratchGuard(*scratchDir);

    CreateDirectoryW(installDir.c_str(), nullptr);
    const DWORD installAttributes = GetFileAttributesW(installDir.c_str());
    if (installAttributes == INVALID_FILE_ATTRIBUTES ||
        (installAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (installAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        std::fwprintf(stderr, L"FastFilesSetup: install directory is missing or a reparse point -- aborting\n");
        return 1;
    }

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
    std::optional<ffsetup::PackageManifest> priorManifest;
    if (isUpgrade) {
        priorManifest = ffsetup::ReadPackageManifest(installDir + L"\\" + kPackageManifestFileName);
        if (!priorManifest || !ValidatePayloadSet(installDir, *priorManifest)) {
            std::fwprintf(stderr, L"FastFilesSetup: prior installation is untrusted or incomplete; automatic rollback unavailable\n");
            return 1;
        }
        if (!StopIndexService()) {
            std::fwprintf(stderr, L"FastFilesSetup: failed to stop FastFilesIndexSvc for upgrade\n");
            return 1;
        }
        priorAccount = ffsetup::QueryIndexServiceAccount();
        if (!priorAccount) {
            std::fwprintf(stderr, L"FastFilesSetup: could not read the prior service account configuration -- aborting\n");
            return 1;
        }
        if (!BackupServiceState(installDir, *priorAccount, *priorManifest)) {
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
            RestoreServiceState(installDir, *scratchDir, clientGroupSid->Get());
        } else {
            // Fresh install: undo every step that may have run so far, not
            // just the service registration. A later step failing at the
            // scheduled task (536), uninstall metadata (543), or startup
            // verification (551) would otherwise leave the scheduled task,
            // uninstall metadata, restrictive ACL, and staged binaries behind.
            ffsetup::UnregisterIndexService();
            ffsetup::UnregisterEngineScheduledTask();
            UnregisterUninstallMetadata();
            ResetInstallDirectoryAcl(installDir);
            DeleteInstalledBinaries(installDir);
        }
    };

    if (!StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kIndexSvcExeName, *sourceManifest) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, ffsetup::kEngineExeName, *sourceManifest) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, kUiExeName, *sourceManifest) ||
        !StageAndInstallBinary(*scratchDir, sourceDir, installDir, kInstallerExeName, *sourceManifest) ||
        !StageAndInstallManifest(*scratchDir, installDir, *sourceManifest)) {
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

int RunUninstall(const std::wstring& runningInstallerPath) {
    UnregisterUninstallMetadata();
    ffsetup::UnregisterEngineScheduledTask();
    ffsetup::UnregisterIndexService();
    ffsetup::DeleteAuthorizedClientGroup();

    bool clean = true;
    const std::wstring expectedSelfPath = GetInstallDirectory() + L"\\" + kInstallerExeName;
    const bool selfIsInstalledImage = !runningInstallerPath.empty() &&
                                      _wcsicmp(runningInstallerPath.c_str(), expectedSelfPath.c_str()) == 0;
    const std::wstring installDir = GetInstallDirectory();
    if (!installDir.empty()) {
        const DWORD rootAttributes = GetFileAttributesW(installDir.c_str());
        if (rootAttributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : 1;
        }
        if ((rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            std::fwprintf(stderr, L"FastFilesSetup: refusing to uninstall through a reparse-point install directory\n");
            return 1;
        }
        const std::wstring searchPattern = installDir + L"\\*";
        WIN32_FIND_DATAW findData{};
        HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                    continue;
                }
                const std::wstring child = installDir + L"\\" + findData.cFileName;
                if (selfIsInstalledImage && _wcsicmp(child.c_str(), expectedSelfPath.c_str()) == 0) {
                    continue;
                }
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                        if (!RemoveDirectoryW(child.c_str())) clean = false;
                    } else {
                        DeleteKnownTree(child);
                        if (GetFileAttributesW(child.c_str()) != INVALID_FILE_ATTRIBUTES) clean = false;
                    }
                } else if (!DeleteFileW(child.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                    clean = false;
                }
            } while (FindNextFileW(findHandle, &findData));
            FindClose(findHandle);
        } else {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) clean = false;
        }
        if (!RemoveDirectoryW(installDir.c_str()) && !selfIsInstalledImage) {
            clean = false;
        }
    }

    RemoveDiagnosticState();

    return clean ? 0 : 1;
}

} // namespace ffinstaller
