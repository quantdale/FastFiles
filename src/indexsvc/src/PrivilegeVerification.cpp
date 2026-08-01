#include "PrivilegeVerification.h"

#include <windows.h>
#include <winioctl.h>
#include <sddl.h>

#include <cstdio>
#include <vector>

#include "ffprotocol/Diagnostics.h"
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

TokenPrivilegeState CaptureTokenPrivilegeState() {
    TokenPrivilegeState state;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return state;

    LUID backupLuid{};
    const bool haveBackupLuid = LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &backupLuid) != FALSE;
    DWORD privilegeBytes = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &privilegeBytes);
    if (haveBackupLuid && privilegeBytes != 0) {
        std::vector<uint8_t> buffer(privilegeBytes);
        if (GetTokenInformation(token, TokenPrivileges, buffer.data(), privilegeBytes, &privilegeBytes)) {
            const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());
            for (DWORD index = 0; index < privileges->PrivilegeCount; ++index) {
                const auto& privilege = privileges->Privileges[index];
                if (privilege.Luid.LowPart == backupLuid.LowPart && privilege.Luid.HighPart == backupLuid.HighPart) {
                    state.backupPrivilegeHeld = true;
                    state.backupPrivilegeEnabled = (privilege.Attributes & SE_PRIVILEGE_ENABLED) != 0;
                    break;
                }
            }
        }
    }

    DWORD userBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &userBytes);
    if (userBytes != 0) {
        std::vector<uint8_t> buffer(userBytes);
        if (GetTokenInformation(token, TokenUser, buffer.data(), userBytes, &userBytes)) {
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            LPWSTR sidText = nullptr;
            if (ConvertSidToStringSidW(user->User.Sid, &sidText) != FALSE) {
                state.accountSid = sidText;
                LocalFree(sidText);
            }
            DWORD nameBytes = 0, domainBytes = 0;
            SID_NAME_USE use{};
            LookupAccountSidW(nullptr, user->User.Sid, nullptr, &nameBytes, nullptr, &domainBytes, &use);
            if (nameBytes != 0) {
                // LookupAccountSidW reports the required character counts;
                // reserve one extra slot so the second call is safe on both
                // APIs that include the terminator and those that report the
                // count excluding it. A local SID can legitimately have no
                // referenced domain, so do not require domainBytes != 0.
                const DWORD nameCapacity = nameBytes + 1;
                const DWORD domainCapacity = domainBytes + 1;
                std::vector<wchar_t> name(nameCapacity, L'\0');
                std::vector<wchar_t> domain(domainCapacity, L'\0');
                DWORD nameChars = nameCapacity;
                DWORD domainChars = domainCapacity;
                if (LookupAccountSidW(nullptr, user->User.Sid, name.data(), &nameChars,
                                       domain.data(), &domainChars, &use)) {
                    state.accountName.assign(name.data());
                    if (domain[0] != L'\0') {
                        std::wstring qualifiedName(domain.data());
                        qualifiedName += L"\\";
                        qualifiedName += state.accountName;
                        state.accountName = qualifiedName;
                    }
                }
            }
        }
    }
    CloseHandle(token);
    return state;
}

RawVolumeOpenOutcome ClassifyRawVolumeOpen(const TokenPrivilegeState& tokenState,
                                           bool opened, DWORD) {
    if (!tokenState.backupPrivilegeHeld) return RawVolumeOpenOutcome::PrivilegeAbsent;
    if (!tokenState.backupPrivilegeEnabled) return RawVolumeOpenOutcome::PrivilegePresentNotEnabled;
    return opened ? RawVolumeOpenOutcome::Succeeded : RawVolumeOpenOutcome::EnabledButDenied;
}

const wchar_t* RawVolumeOpenOutcomeName(RawVolumeOpenOutcome outcome) {
    switch (outcome) {
        case RawVolumeOpenOutcome::PrivilegeAbsent: return L"privilege-absent";
        case RawVolumeOpenOutcome::PrivilegePresentNotEnabled: return L"privilege-present-not-enabled";
        case RawVolumeOpenOutcome::EnabledButDenied: return L"enabled-but-denied";
        case RawVolumeOpenOutcome::Succeeded: return L"succeeded";
    }
    return L"unknown";
}

void LogRawVolumeOpenDiagnostic(const std::wstring& volumePath,
                                const TokenPrivilegeState& tokenState,
                                RawVolumeOpenOutcome outcome, DWORD errorCode) {
    ffprotocol::DiagnosticEvent event;
    event.category = ffprotocol::DiagnosticCategory::IndexingError;
    event.path = volumePath;
    event.state = L"raw-volume-open";
    event.outcome = RawVolumeOpenOutcomeName(outcome);
    event.accountName = tokenState.accountName;
    event.accountSid = tokenState.accountSid;
    event.privilegeHeld = tokenState.backupPrivilegeHeld;
    event.privilegeEnabled = tokenState.backupPrivilegeEnabled;
    event.errorCode = errorCode;
    ffprotocol::AppendDiagnostic(event);
}

BackupPrivilegeProbeResult ProbeBackupPrivilegeSufficiency() {
    BackupPrivilegeProbeResult result;
    EnsureBackupPrivilegeEnabled();
    const TokenPrivilegeState tokenState = CaptureTokenPrivilegeState();
    result.privilegeEnabled = tokenState.backupPrivilegeEnabled;
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
