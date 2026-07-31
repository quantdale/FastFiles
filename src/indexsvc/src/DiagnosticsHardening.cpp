#include "DiagnosticsHardening.h"

#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>
#include <werapi.h>

#include "ffsetup/Identifiers.h"
#include "ffsetup/SecurityDescriptors.h"

#pragma comment(lib, "wer.lib")
#pragma comment(lib, "shell32.lib")

namespace ffindexsvc {

namespace {

std::wstring GetProgramDataFastFilesDir() {
    PWSTR programDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programDataPath))) {
        return L"";
    }
    std::wstring result(programDataPath);
    CoTaskMemFree(programDataPath);
    result += L"\\FastFiles";
    return result;
}

void ApplyAdminOnlyAcl(const std::wstring& path) {
    auto descriptor = ffsetup::BuildAdminOnlySecurityDescriptor();
    if (!descriptor) {
        return;
    }
    PACL dacl = nullptr;
    BOOL daclPresent = FALSE;
    BOOL daclDefaulted = FALSE;
    if (GetSecurityDescriptorDacl(descriptor->attributes.lpSecurityDescriptor, &daclPresent, &dacl, &daclDefaulted)
        && daclPresent) {
        SetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
    }
}

std::wstring EnsureDirectory(const std::wstring& path) {
    CreateDirectoryW(path.c_str(), nullptr);
    ApplyAdminOnlyAcl(path);
    return path;
}

} // namespace

std::wstring EnsureAdminOnlyLogDirectory() {
    const std::wstring baseDir = GetProgramDataFastFilesDir();
    if (baseDir.empty()) {
        return L"";
    }
    CreateDirectoryW(baseDir.c_str(), nullptr);
    ApplyAdminOnlyAcl(baseDir);
    return EnsureDirectory(baseDir + L"\\Logs");
}

void HardenCrashDumpHandling() {
    // Primary mitigation: exclude this binary from Windows Error Reporting
    // entirely, so no crash dump is ever written to the default
    // user-readable location (%LOCALAPPDATA%\CrashDumps).
    WerAddExcludedApplication(ffsetup::kIndexSvcExeName, FALSE);

    // Defense in depth: if WER exclusion is later reverted (e.g. by an
    // admin troubleshooting a crash), redirect any LocalDumps collection
    // for this binary to an admin-only directory instead of the default.
    const std::wstring baseDir = GetProgramDataFastFilesDir();
    if (baseDir.empty()) {
        return;
    }
    const std::wstring dumpDir = EnsureDirectory(baseDir + L"\\CrashDumps");

    const std::wstring keyPath =
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\" +
        std::wstring(ffsetup::kIndexSvcExeName);

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        == ERROR_SUCCESS) {
        RegSetValueExW(key, L"DumpFolder", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(dumpDir.c_str()),
                        static_cast<DWORD>((dumpDir.size() + 1) * sizeof(wchar_t)));
        DWORD dumpCount = 5;
        RegSetValueExW(key, L"DumpCount", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dumpCount), sizeof(dumpCount));
        DWORD dumpType = 1; // mini-dump: enough to debug, minimizes sensitive memory captured
        RegSetValueExW(key, L"DumpType", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dumpType), sizeof(dumpType));
        RegCloseKey(key);
    }
}

} // namespace ffindexsvc
