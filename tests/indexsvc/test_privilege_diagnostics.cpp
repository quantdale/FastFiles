#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "PrivilegeVerification.h"

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

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

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
