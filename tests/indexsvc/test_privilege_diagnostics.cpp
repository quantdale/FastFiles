#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "PrivilegeVerification.h"
#include "ffprotocol/Diagnostics.h"
#include "../TestSupport.h"

using namespace fftest;

int main() {
    using namespace ffindexsvc;
    TokenPrivilegeState absent;
    Check(ClassifyRawVolumeOpen(absent, false, ERROR_ACCESS_DENIED) == RawVolumeOpenOutcome::PrivilegeAbsent,
          "absent privilege is classified distinctly");

    TokenPrivilegeState held;
    held.backupPrivilegeHeld = true;
    Check(ClassifyRawVolumeOpen(held, false, ERROR_ACCESS_DENIED) == RawVolumeOpenOutcome::PrivilegePresentNotEnabled,
          "held but disabled privilege is classified distinctly");

    held.backupPrivilegeEnabled = true;
    Check(ClassifyRawVolumeOpen(held, false, ERROR_ACCESS_DENIED) == RawVolumeOpenOutcome::EnabledButDenied,
          "enabled privilege with denied open is classified distinctly");
    Check(ClassifyRawVolumeOpen(held, true, ERROR_SUCCESS) == RawVolumeOpenOutcome::Succeeded,
          "successful open is classified distinctly");
    Check(std::wstring(RawVolumeOpenOutcomeName(RawVolumeOpenOutcome::EnabledButDenied)) == L"enabled-but-denied",
          "classification has a stable structured name");

    // Candidate matrix classification (resolve-raw-volume-privilege-
    // insufficiency task 1.2): five distinguishable outcomes.
    Check(ClassifyCandidateMatrixRow(false, false, false, false) == CandidateMatrixOutcome::PrivilegeAbsent,
          "matrix: absent privilege classified distinctly");
    Check(ClassifyCandidateMatrixRow(true, false, false, false) == CandidateMatrixOutcome::PrivilegePresentNotEnabled,
          "matrix: held-but-disabled privilege classified distinctly");
    Check(ClassifyCandidateMatrixRow(true, true, false, false) == CandidateMatrixOutcome::OpenDenied,
          "matrix: enabled-but-denied open classified distinctly");
    Check(ClassifyCandidateMatrixRow(true, true, true, false) == CandidateMatrixOutcome::OpenSucceededJournalFailed,
          "matrix: open succeeded but journal failed classified distinctly");
    Check(ClassifyCandidateMatrixRow(true, true, true, true) == CandidateMatrixOutcome::OpenSucceededJournalSucceeded,
          "matrix: open and journal both succeeded classified distinctly");
    Check(std::wstring(CandidateMatrixOutcomeName(CandidateMatrixOutcome::OpenSucceededJournalSucceeded))
              == L"open-succeeded-journal-succeeded",
          "matrix: classification has a stable structured name");

    // Group context classification: well-known elevated-group membership is
    // detected from raw group SIDs so a candidate pass cannot be silently
    // explained by unrelated administrative state.
    TokenGroupContext adminContext = ClassifyTokenGroups({L"S-1-5-32-544", L"S-1-5-11"});
    Check(adminContext.administrators && !adminContext.localSystem && adminContext.authenticatedUsers,
          "matrix: Administrators and Authenticated Users detected, LocalSystem not");
    TokenGroupContext systemContext = ClassifyTokenGroups({L"S-1-5-18"});
    Check(systemContext.localSystem && !systemContext.administrators,
          "matrix: LocalSystem detected distinctly from Administrators");
    TokenGroupContext backupOpsContext = ClassifyTokenGroups({L"S-1-5-32-551"});
    Check(backupOpsContext.backupOperators,
          "matrix: Backup Operators detected");
    TokenGroupContext noneContext = ClassifyTokenGroups({L"S-1-5-32-545"});
    Check(!noneContext.administrators && !noneContext.localSystem && !noneContext.backupOperators,
          "matrix: ordinary Users group is not flagged as elevated");

    // Group context formatting is compact and log-safe.
    Check(FormatGroupContext(adminContext) == L"admins=1;system=0;backupOps=0;authUsers=1",
          "matrix: group context formats compactly");

    // Startup-token diagnostic (resolve-raw-volume-privilege-insufficiency
    // task 2.3): running it must persist a diagnostic record describing the
    // real process token without crashing in a plain test process (where the
    // account is the test user and SeBackupPrivilege may be absent -- both are
    // valid records). The event is appended to the diagnostics log; the log
    // path is a per-machine temp file so this is side-effect-free to tests.
    const std::wstring logPath = ffprotocol::DiagnosticLogPath();
    std::error_code fileError;
    const ULONGLONG logBytesBefore = logPath.empty()
        ? 0 : static_cast<ULONGLONG>(std::filesystem::file_size(std::filesystem::path(logPath), fileError));
    LogStartupTokenDiagnostic();
    Check(!logPath.empty(), "startup-token: diagnostic log path resolves");

    // The installer's VerifyServiceStartedFresh scans only the log bytes
    // written after the (re)start for `state=startup-token` and treats an
    // explicit `privilegeEnabled=0` as a failed fresh token (task 2.3).
    // Mirror that exact from-offset scan and assert the field contract so a
    // serializer/parser drift breaks a unit test rather than the installer.
    {
        bool sawStartupToken = false;
        bool hasAccountIdentity = false;
        bool hasPrivilegeState = false;
        std::wifstream log(logPath, std::ios::binary);
        log.seekg(static_cast<std::streamoff>(logBytesBefore));
        std::wstring line;
        while (std::getline(log, line)) {
            if (line.find(L"state=startup-token") != std::wstring::npos) {
                sawStartupToken = true;
                hasAccountIdentity = line.find(L"account=") != std::wstring::npos
                    && line.find(L"sid=") != std::wstring::npos;
                hasPrivilegeState = line.find(L"privilegeHeld=") != std::wstring::npos
                    && line.find(L"privilegeEnabled=") != std::wstring::npos;
            }
        }
        Check(sawStartupToken,
              "startup-token: the freshly written log contains the record the installer greps for");
        Check(hasAccountIdentity,
              "startup-token: the record names the verified account/SID -- the auditable fresh-token identity");
        Check(hasPrivilegeState,
              "startup-token: the record exposes SeBackupPrivilege held/enabled state for the verification step");
    }

    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
