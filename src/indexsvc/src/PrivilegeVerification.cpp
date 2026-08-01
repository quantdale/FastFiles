#include "PrivilegeVerification.h"

#include <windows.h>
#include <winioctl.h>

#include <cstdio>

#include "VolumeEnumeration.h"

namespace ffindexsvc {

bool EnsureBackupPrivilegeEnabled() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const bool adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr) &&
        GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    CloseHandle(token);
    return adjusted;
}

BackupPrivilegeProbeResult ProbeBackupPrivilegeSufficiency() {
    BackupPrivilegeProbeResult result;
    result.privilegeEnabled = EnsureBackupPrivilegeEnabled();
    if (!result.privilegeEnabled) {
        return result;
    }

    auto volumes = EnumerateFixedNtfsVolumes();
    result.volumeFound = !volumes.empty();
    if (!result.volumeFound) {
        return result;
    }

    result.driveLetter = volumes.front().driveLetter;
    wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', result.driveLetter, L':', L'\0'};

    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    result.volumeOpened = volumeHandle != INVALID_HANDLE_VALUE;
    if (!result.volumeOpened) {
        result.volumeOpenError = GetLastError();
        return result;
    }

    USN_JOURNAL_DATA_V0 journalData{};
    DWORD bytesReturned = 0;
    result.journalQueried = DeviceIoControl(
        volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData,
        sizeof(journalData), &bytesReturned, nullptr) != FALSE;
    result.journalQueryError = result.journalQueried ? ERROR_SUCCESS : GetLastError();
    CloseHandle(volumeHandle);
    return result;
}

void VerifyBackupPrivilegeSufficiency() {
    const BackupPrivilegeProbeResult result = ProbeBackupPrivilegeSufficiency();
    if (!result.privilegeEnabled) {
        std::fwprintf(stderr, L"FastFilesIndexSvc: [privilege-check] SeBackupPrivilege is not held/enabled -- skipping (task 7.1 check)\n");
        return;
    }
    if (!result.volumeFound) {
        std::fwprintf(stderr, L"FastFilesIndexSvc: [privilege-check] no fixed NTFS/ReFS volume found to test against\n");
        return;
    }
    wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', result.driveLetter, L':', L'\0'};
    if (!result.volumeOpened) {
        std::fwprintf(stderr, L"FastFilesIndexSvc: [privilege-check] FAIL -- could not open raw volume handle for %ls (error %lu)\n",
                       volumePath, result.volumeOpenError);
        return;
    }
    if (result.journalQueried) {
        std::fwprintf(stderr,
            L"FastFilesIndexSvc: [privilege-check] PASS -- SeBackupPrivilege alone opened %ls and queried its USN journal\n",
            volumePath);
    } else {
        // Not necessarily a failure of the privilege-minimization premise
        // -- e.g. ERROR_JOURNAL_NOT_ACTIVE just means USN journaling isn't
        // enabled on this volume, which is orthogonal to whether the
        // *handle open* itself needed more than SeBackupPrivilege (that
        // already succeeded above).
        std::fwprintf(stderr,
            L"FastFilesIndexSvc: [privilege-check] volume handle opened, but FSCTL_QUERY_USN_JOURNAL failed (error %lu) for %ls\n",
            result.journalQueryError, volumePath);
    }
}

} // namespace ffindexsvc
