#include "DiagnosticsHardening.h"

#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>
#include <werapi.h>

#include <iterator>

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

void WriteServiceLifecycleLog(const wchar_t* message) {
    if (message == nullptr) {
        return;
    }
    const std::wstring logDir = EnsureAdminOnlyLogDirectory();
    if (logDir.empty()) {
        return;
    }
    const std::wstring logPath = logDir + L"\\service.log";
    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    LARGE_INTEGER fileSize{};
    if (GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == 0) {
        constexpr wchar_t kByteOrderMark = 0xFEFF;
        DWORD bomWritten = 0;
        WriteFile(file, &kByteOrderMark, sizeof(kByteOrderMark), &bomWritten, nullptr);
    }
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t line[512];
    const int length = swprintf_s(
        line, std::size(line), L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %ls\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, message);
    if (length > 0) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length * sizeof(wchar_t)), &written, nullptr);
    }
    CloseHandle(file);
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
