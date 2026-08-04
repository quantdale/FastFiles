#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ffindexsvc {

// Enables SeBackupPrivilege on this process's token (a no-op, returning
// true, if already enabled). Holding the privilege is not enough on its
// own -- it must be explicitly enabled before FILE_FLAG_BACKUP_SEMANTICS
// opens bypass the normal ACL check. Idempotent and cheap enough to call
// before every raw volume open (VolumeScanner, UsnJournalReader), not
// just once at service startup.
bool EnsureBackupPrivilegeEnabled();

// A single candidate privilege's held/enabled state in the process token.
struct PrivilegeFlag {
    std::wstring name;
    bool held = false;
    bool enabled = false;
};

// Well-known elevated-group membership, captured so a candidate's apparent
// pass cannot be silently explained by unrelated administrative state on the
// test host (resolve-raw-volume-privilege-insufficiency design.md, evidence-
// driven candidate matrix risk mitigation).
struct TokenGroupContext {
    bool administrators = false;
    bool localSystem = false;
    bool backupOperators = false;
    bool authenticatedUsers = false;
};

struct TokenPrivilegeState {
    bool backupPrivilegeHeld = false;
    bool backupPrivilegeEnabled = false;
    std::wstring accountName;
    std::wstring accountSid;
    TokenGroupContext groups;
    std::vector<PrivilegeFlag> privileges;
};

enum class RawVolumeOpenOutcome {
    PrivilegeAbsent,
    PrivilegePresentNotEnabled,
    EnabledButDenied,
    Succeeded,
};

TokenPrivilegeState CaptureTokenPrivilegeState();
RawVolumeOpenOutcome ClassifyRawVolumeOpen(const TokenPrivilegeState& tokenState,
                                           bool opened, DWORD errorCode);
const wchar_t* RawVolumeOpenOutcomeName(RawVolumeOpenOutcome outcome);
void LogRawVolumeOpenDiagnostic(const std::wstring& volumePath,
                                const TokenPrivilegeState& tokenState,
                                RawVolumeOpenOutcome outcome, DWORD errorCode);

// --- Candidate privilege matrix (resolve-raw-volume-privilege-insufficiency
// task 1.2) ---------------------------------------------------------------
//
// The diagnosis change proved SeBackupPrivilege (held + enabled) is not
// sufficient to open a raw volume device. These types and functions extend
// the harness so a candidate matrix can record, for every account/right
// candidate: account SID, group context, held/enabled privilege state,
// raw-volume CreateFileW result, USN journal control/read results, service
// registration order, and exact Win32 error codes.

enum class CandidateMatrixOutcome {
    PrivilegeAbsent,
    PrivilegePresentNotEnabled,
    OpenDenied,
    OpenSucceededJournalSucceeded,
    OpenSucceededJournalFailed,
};

struct CandidateMatrixRow {
    std::wstring candidateId;
    std::wstring privilegeName;
    std::wstring volumePath;
    TokenPrivilegeState tokenState;
    bool privilegeHeld = false;       // the named candidate privilege's state
    bool privilegeEnabled = false;
    bool volumeOpened = false;
    DWORD volumeOpenError = ERROR_SUCCESS;
    bool journalQueried = false;
    DWORD journalQueryError = ERROR_SUCCESS;
    bool journalRead = false;
    DWORD journalReadError = ERROR_SUCCESS;
    uint32_t registrationOrder = 0;
    CandidateMatrixOutcome outcome = CandidateMatrixOutcome::PrivilegeAbsent;
};

// Pure: classifies a completed candidate row from its raw held/enabled/open/
// journal signals. Unit-testable independent of a real token or volume.
CandidateMatrixOutcome ClassifyCandidateMatrixRow(bool privilegeHeld, bool privilegeEnabled,
                                                  bool volumeOpened, bool journalQueried);
const wchar_t* CandidateMatrixOutcomeName(CandidateMatrixOutcome outcome);

// Pure: classifies a token's group SID set into well-known elevated-group
// membership, and formats a compact log-safe summary. Unit-testable.
TokenGroupContext ClassifyTokenGroups(const std::vector<std::wstring>& groupSids);
std::wstring FormatGroupContext(const TokenGroupContext& groups);

// Enables an arbitrary named privilege on this process's token (a no-op,
// returning true, if already enabled). Generalizes EnsureBackupPrivilegeEnabled
// so each matrix candidate's privilege is enabled before its raw-volume open,
// making rows comparable. Idempotent and cheap.
bool EnsurePrivilegeEnabled(const std::wstring& privilegeName);

// Builds and classifies one candidate-matrix row: enables the named candidate
// privilege, captures the real process token (account, group context, the
// candidate privilege set's held/enabled state), opens the raw volume
// (`\\.\D:`-style path for the supplied drive letter) with
// FILE_FLAG_BACKUP_SEMANTICS, and queries the USN journal via
// FSCTL_QUERY_USN_JOURNAL, recording exact Win32 errors throughout. Called
// once per candidate by the matrix runner (task 1.3); the open/journal calls
// require a real service token and a real NTFS volume, so this path is not
// exercised by the unit tests.
CandidateMatrixRow CaptureCandidateMatrixRow(const std::wstring& candidateId,
                                             const std::wstring& privilegeName,
                                             wchar_t driveLetter,
                                             uint32_t registrationOrder);

// Persists a candidate row as a structured diagnostic event.
void LogCandidateMatrixRow(const CandidateMatrixRow& row);


struct BackupPrivilegeProbeResult {
    bool privilegeEnabled = false;
    bool volumeFound = false;
    bool volumeOpened = false;
    bool journalQueried = false;
    wchar_t driveLetter = L'\0';
    DWORD volumeOpenError = ERROR_SUCCESS;
    DWORD journalQueryError = ERROR_SUCCESS;
};

// Structured form consumed by fftest and the runtime harness. The service
// startup self-check below is a logging wrapper over this same probe.
BackupPrivilegeProbeResult ProbeBackupPrivilegeSufficiency();

// Task 7.1: verify at startup that the token FastFilesIndexSvc actually runs
// under holds SeBackupPrivilege -- the backup-semantics right raw-volume/USN
// reading depends on (design.md D4). The service runs as LocalSystem (the
// constrained privileged broker), which inherently holds the privilege; this
// probe confirms it is present and enableable in the real service token so the
// raw-volume scanner (index-storage-and-scanning) can rely on it.
//
// Deliberately runs as a startup self-check baked into the shipped
// service, rather than a one-off manual tool: verifying this once in a
// developer's sandbox wouldn't prove anything about the production
// deployment (running under the actual service account), so this checks it
// every time the real, installed service starts. Logs the result; a
// negative result does not fail service startup, since degraded mode
// remains a valid operating state even if the privileged path is unable
// to open a raw volume.
void VerifyBackupPrivilegeSufficiency();

// Task 2.3 (resolve-raw-volume-privilege-insufficiency): emits a startup
// diagnostic record describing the verified token -- account name/SID, group
// context, and SeBackupPrivilege held/enabled state -- that every subsequent
// raw-volume scan/journal call in this process runs under. The spec scenario
// "Restart uses the newly granted token" requires the diagnostic record to
// describe the token used by the raw-volume call, so the installer restart
// (fresh token) plus this record makes the evidence auditable end-to-end.
void LogStartupTokenDiagnostic();

} // namespace ffindexsvc
