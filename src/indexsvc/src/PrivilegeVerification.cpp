#include "PrivilegeVerification.h"

#include <windows.h>
#include <winioctl.h>
#include <sddl.h>

#include <cstdio>
#include <vector>

#include "ffprotocol/Diagnostics.h"
#include "VolumeEnumeration.h"

namespace ffindexsvc {

bool EnsurePrivilegeEnabled(const std::wstring& privilegeName) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, privilegeName.c_str(), &luid)) {
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

bool EnsureBackupPrivilegeEnabled() {
    return EnsurePrivilegeEnabled(SE_BACKUP_NAME);
}

TokenPrivilegeState CaptureTokenPrivilegeState() {
    TokenPrivilegeState state;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return state;

    // Candidate privilege names evaluated by the matrix (task 1.1). SeBackup
    // is reported both in `privileges` and via the legacy backupPrivilege*
    // fields so the existing scan-path call site and its tests stay unchanged.
    struct NamedLuid {
        std::wstring name;
        LUID luid{};
        bool luidKnown = false;
    };
    NamedLuid candidates[4] = {
        {SE_BACKUP_NAME, {}, false},
        {SE_RESTORE_NAME, {}, false},
        {SE_MANAGE_VOLUME_NAME, {}, false},
        {SE_SECURITY_NAME, {}, false},
    };
    for (auto& candidate : candidates) {
        candidate.luidKnown = LookupPrivilegeValueW(nullptr, candidate.name.c_str(), &candidate.luid) != FALSE;
    }

    DWORD privilegeBytes = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &privilegeBytes);
    if (privilegeBytes != 0) {
        std::vector<uint8_t> buffer(privilegeBytes);
        if (GetTokenInformation(token, TokenPrivileges, buffer.data(), privilegeBytes, &privilegeBytes)) {
            const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());
            for (DWORD index = 0; index < privileges->PrivilegeCount; ++index) {
                const auto& entry = privileges->Privileges[index];
                for (auto& candidate : candidates) {
                    if (candidate.luidKnown &&
                        entry.Luid.LowPart == candidate.luid.LowPart &&
                        entry.Luid.HighPart == candidate.luid.HighPart) {
                        PrivilegeFlag flag;
                        flag.name = candidate.name;
                        flag.held = true;
                        flag.enabled = (entry.Attributes & SE_PRIVILEGE_ENABLED) != 0;
                        state.privileges.push_back(flag);
                        if (candidate.name == SE_BACKUP_NAME) {
                            state.backupPrivilegeHeld = true;
                            state.backupPrivilegeEnabled = flag.enabled;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Group context: well-known elevated-group membership, so a candidate's
    // apparent pass cannot be silently explained by unrelated administrative
    // state on the test host (design.md evidence-driven matrix mitigation).
    std::vector<std::wstring> groupSids;
    DWORD groupBytes = 0;
    GetTokenInformation(token, TokenGroups, nullptr, 0, &groupBytes);
    if (groupBytes != 0) {
        std::vector<uint8_t> buffer(groupBytes);
        if (GetTokenInformation(token, TokenGroups, buffer.data(), groupBytes, &groupBytes)) {
            const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
            for (DWORD index = 0; index < groups->GroupCount; ++index) {
                LPWSTR sidText = nullptr;
                if (ConvertSidToStringSidW(groups->Groups[index].Sid, &sidText) != FALSE) {
                    groupSids.emplace_back(sidText);
                    LocalFree(sidText);
                }
            }
        }
    }
    state.groups = ClassifyTokenGroups(groupSids);

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

TokenGroupContext ClassifyTokenGroups(const std::vector<std::wstring>& groupSids) {
    TokenGroupContext context;
    for (const auto& sid : groupSids) {
        if (sid == L"S-1-5-32-544") context.administrators = true;
        else if (sid == L"S-1-5-18") context.localSystem = true;
        else if (sid == L"S-1-5-32-551") context.backupOperators = true;
        else if (sid == L"S-1-5-11") context.authenticatedUsers = true;
    }
    return context;
}

std::wstring FormatGroupContext(const TokenGroupContext& groups) {
    return L"admins=" + std::wstring(groups.administrators ? L"1" : L"0")
         + L";system=" + (groups.localSystem ? L"1" : L"0")
         + L";backupOps=" + (groups.backupOperators ? L"1" : L"0")
         + L";authUsers=" + (groups.authenticatedUsers ? L"1" : L"0");
}

CandidateMatrixOutcome ClassifyCandidateMatrixRow(bool privilegeHeld, bool privilegeEnabled,
                                                  bool volumeOpened, bool journalQueried) {
    if (!privilegeHeld) return CandidateMatrixOutcome::PrivilegeAbsent;
    if (!privilegeEnabled) return CandidateMatrixOutcome::PrivilegePresentNotEnabled;
    if (!volumeOpened) return CandidateMatrixOutcome::OpenDenied;
    if (!journalQueried) return CandidateMatrixOutcome::OpenSucceededJournalFailed;
    return CandidateMatrixOutcome::OpenSucceededJournalSucceeded;
}

const wchar_t* CandidateMatrixOutcomeName(CandidateMatrixOutcome outcome) {
    switch (outcome) {
        case CandidateMatrixOutcome::PrivilegeAbsent: return L"privilege-absent";
        case CandidateMatrixOutcome::PrivilegePresentNotEnabled: return L"privilege-present-not-enabled";
        case CandidateMatrixOutcome::OpenDenied: return L"open-denied";
        case CandidateMatrixOutcome::OpenSucceededJournalFailed: return L"open-succeeded-journal-failed";
        case CandidateMatrixOutcome::OpenSucceededJournalSucceeded: return L"open-succeeded-journal-succeeded";
    }
    return L"unknown";
}

CandidateMatrixRow CaptureCandidateMatrixRow(const std::wstring& candidateId,
                                             const std::wstring& privilegeName,
                                             wchar_t driveLetter,
                                             uint32_t registrationOrder) {
    CandidateMatrixRow row;
    row.candidateId = candidateId;
    row.privilegeName = privilegeName;
    row.registrationOrder = registrationOrder;
    wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};
    row.volumePath = volumePath;

    // Make the candidate privilege enabled before the open so every row is
    // comparable -- holding a privilege is not enough; it must be enabled
    // (UsnJournalReader.cpp / design.md D4). Idempotent and cheap.
    EnsurePrivilegeEnabled(privilegeName);

    row.tokenState = CaptureTokenPrivilegeState();
    for (const auto& flag : row.tokenState.privileges) {
        if (flag.name == privilegeName) {
            row.privilegeHeld = flag.held;
            row.privilegeEnabled = flag.enabled;
            break;
        }
    }

    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    row.volumeOpened = volumeHandle != INVALID_HANDLE_VALUE;
    row.volumeOpenError = row.volumeOpened ? ERROR_SUCCESS : GetLastError();

    if (row.volumeOpened) {
        USN_JOURNAL_DATA_V0 journalData{};
        DWORD bytesReturned = 0;
        row.journalQueried = DeviceIoControl(
            volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData,
            sizeof(journalData), &bytesReturned, nullptr) != FALSE;
        row.journalQueryError = row.journalQueried ? ERROR_SUCCESS : GetLastError();

        // "Read" half of the journal evidence: a minimal, non-blocking
        // FSCTL_READ_USN_JOURNAL from the journal's current NextUsn. With
        // BytesToWaitFor=0/Timeout=0 it returns immediately (just the next
        // USN when no new records exist), so success confirms the same handle
        // can perform the read the scan path needs. A documented non-privilege
        // condition such as ERROR_JOURNAL_NOT_ACTIVE is recorded as an exact
        // code and interpreted by the selection step (task 1.4), not here.
        if (row.journalQueried) {
            READ_USN_JOURNAL_DATA_V0 readRequest{};
            readRequest.StartUsn = journalData.NextUsn;
            readRequest.ReasonMask = 0xFFFFFFFF;
            readRequest.ReturnOnlyOnClose = FALSE;
            readRequest.Timeout = 0;
            readRequest.BytesToWaitFor = 0;
            readRequest.UsnJournalID = journalData.UsnJournalID;
            std::vector<uint8_t> readBuffer(4096);
            DWORD readBytesReturned = 0;
            row.journalRead = DeviceIoControl(
                volumeHandle, FSCTL_READ_USN_JOURNAL, &readRequest, sizeof(readRequest),
                readBuffer.data(), static_cast<DWORD>(readBuffer.size()),
                &readBytesReturned, nullptr) != FALSE;
            row.journalReadError = row.journalRead ? ERROR_SUCCESS : GetLastError();
        } else {
            row.journalRead = false;
            row.journalReadError = ERROR_SUCCESS;
        }
        CloseHandle(volumeHandle);
    } else {
        row.journalQueried = false;
        row.journalQueryError = ERROR_SUCCESS;
        row.journalRead = false;
        row.journalReadError = ERROR_SUCCESS;
    }

    row.outcome = ClassifyCandidateMatrixRow(row.privilegeHeld, row.privilegeEnabled,
                                             row.volumeOpened, row.journalQueried);
    return row;
}

void LogCandidateMatrixRow(const CandidateMatrixRow& row) {
    ffprotocol::DiagnosticEvent event;
    event.category = ffprotocol::DiagnosticCategory::IndexingError;
    event.path = row.volumePath;
    event.state = L"candidate-matrix";
    event.outcome = CandidateMatrixOutcomeName(row.outcome);
    event.accountName = row.tokenState.accountName;
    event.accountSid = row.tokenState.accountSid;
    event.privilegeHeld = row.privilegeHeld;
    event.privilegeEnabled = row.privilegeEnabled;
    event.errorCode = row.volumeOpenError;
    event.candidateId = row.candidateId;
    event.privilegeName = row.privilegeName;
    event.groupContext = FormatGroupContext(row.tokenState.groups);
    event.journalQueried = row.journalQueried;
    event.journalQueryError = row.journalQueryError;
    event.journalRead = row.journalRead;
    event.journalReadError = row.journalReadError;
    event.registrationOrder = row.registrationOrder;
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

void LogStartupTokenDiagnostic() {
    const TokenPrivilegeState tokenState = CaptureTokenPrivilegeState();
    ffprotocol::DiagnosticEvent event;
    event.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
    event.state = L"startup-token";
    event.outcome = tokenState.backupPrivilegeEnabled ? L"backup-privilege-ready" : L"backup-privilege-unavailable";
    event.accountName = tokenState.accountName;
    event.accountSid = tokenState.accountSid;
    event.privilegeHeld = tokenState.backupPrivilegeHeld;
    event.privilegeEnabled = tokenState.backupPrivilegeEnabled;
    event.groupContext = FormatGroupContext(tokenState.groups);
    ffprotocol::AppendDiagnostic(event);
    std::fwprintf(stderr, L"FastFilesIndexSvc: [startup-token] account=%ls sid=%ls backup=%ls groups=[%ls]\n",
                  tokenState.accountName.c_str(), tokenState.accountSid.c_str(),
                  tokenState.backupPrivilegeEnabled ? L"held-enabled" : L"not-enabled",
                  FormatGroupContext(tokenState.groups).c_str());
}

} // namespace ffindexsvc
