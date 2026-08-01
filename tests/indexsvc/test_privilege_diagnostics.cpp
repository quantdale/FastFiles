#include <cstdio>
#include <cstdlib>
#include <string>

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
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
