// PRIVILEGED TRUST-STORE INTEGRATION SUITE -- opt-in only.
//
// This is not part of ordinary FastFiles development. It exercises the real
// production WinTrust decision against a disposable, non-production signer
// installed into CurrentUser\Root (via AuthenticodeFixture.ps1's
// `certutil -user -f -addstore Root`), covering:
//   * a trusted signer is accepted before its embedded validity expires;
//   * the identical signed PE bytes are rejected by WinTrust after the
//     signer's certificate transitions to expired (Authenticode longevity);
//   * an RFC3161-timestamped signature remains valid across that same
//     expiry transition.
//
// Requirements to run this target at all:
//   - a non-elevated Windows user account is sufficient (CurrentUser\Root is
//     a per-user store), but the host's ProtectedRoots ACL/policy must
//     actually allow that store write -- some hardened/hosted images block
//     it, in which case this suite fails closed rather than fabricating a
//     pass (see .agent/workstate/fastfiles-production-validation-state.md);
//   - `signtool.exe` (Windows SDK) on PATH or under a discovered SDK bin dir;
//   - `pwsh.exe` on PATH.
//
// This is NEVER part of the default CTest suite: it is registered only when
// FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS is explicitly turned ON (see
// tests/engine/CMakeLists.txt and verify/run_privileged_trust_tests.ps1).
// Ordinary Debug/release-test/analyze development, and hosted CI, must never
// need to enable this option. See AGENTS.md "Normal FastFiles development".
//
// The certificates this suite generates are disposable, uniquely-named,
// non-production test material (label "TEST CERTIFICATE / NON-PRODUCTION")
// created and removed entirely within this process's run; they are never
// production signer material and never satisfy release-signing evidence.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "AuthenticodeFixtureSupport.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestExpiredWinTrustFixture() {
    const auto modulePath = CurrentModulePath();
    if (!modulePath.has_value()) {
        Check(false, "expiry WinTrust fixture requires the current test executable path");
        return;
    }
    const std::filesystem::path scriptPath = modulePath->parent_path() / L"AuthenticodeFixture.ps1";
    Check(std::filesystem::exists(scriptPath), "expiry WinTrust fixture: fixture helper is deployed beside the test");
    if (!std::filesystem::exists(scriptPath)) {
        return;
    }

    const auto fixtureRoot = CreateFixtureRoot();
    Check(fixtureRoot.has_value(), "expiry WinTrust fixture: isolated temporary directory is created");
    if (!fixtureRoot.has_value()) {
        return;
    }

    TestRealWinTrustFixture(scriptPath, *fixtureRoot, L"Expired",
                            "expiry WinTrust fixture: short-lived test certificate is generated and signed",
                            /*expectedAcceptance=*/true, /*transitionToExpired=*/true);

    std::error_code error;
    const bool removed = std::filesystem::remove(*fixtureRoot, error) && !error;
    Check(removed, "expiry WinTrust fixture: isolated temporary directory is empty after cleanup");
}

void TestTimestampedWinTrustFixture() {
    // Keep the CurrentUser\Root trust-store mutation in its own process-level
    // CTest case. Windows can retain a provider/store handle briefly after a
    // WinTrust close; isolating the timestamped case prevents that unrelated
    // cache lifetime from changing the expiry evidence.
    const auto modulePath = CurrentModulePath();
    if (!modulePath.has_value()) {
        Check(false, "timestamped WinTrust fixture requires the current test executable path");
        return;
    }
    const std::filesystem::path scriptPath = modulePath->parent_path() / L"AuthenticodeFixture.ps1";
    Check(std::filesystem::exists(scriptPath), "timestamped WinTrust fixture helper is deployed beside the test");
    if (!std::filesystem::exists(scriptPath)) {
        return;
    }

    const auto fixtureRoot = CreateFixtureRoot();
    Check(fixtureRoot.has_value(), "timestamped WinTrust fixture creates an isolated temporary directory");
    if (!fixtureRoot.has_value()) {
        return;
    }

    const int workerExit = RunTimestampedWorkerProcess(*modulePath, *fixtureRoot);
    Check(workerExit == 0,
          "timestamped WinTrust fixture: worker proves timestamp longevity before parent cleanup");

    const std::filesystem::path statePath = *fixtureRoot / L"TimestampedExpired.state.json";
    const std::filesystem::path outputPath = *fixtureRoot / L"TimestampedExpired.exe";
    const std::filesystem::path thumbprintPath = outputPath.wstring() + L".thumbprint";
    int cleanupExit = RunFixtureScript(scriptPath, {
        {L"-Action", L"Cleanup"},
        {L"-StatePath", statePath.wstring()}
    });
    if (cleanupExit != 0) {
        Sleep(1000);
        cleanupExit = RunFixtureScript(scriptPath, {
            {L"-Action", L"Cleanup"},
            {L"-StatePath", statePath.wstring()}
        });
    }
    Check(cleanupExit == 0,
          "timestamped WinTrust fixture: parent removes the trusted root after the worker exits");
    Check(!std::filesystem::exists(outputPath) && !std::filesystem::exists(statePath) &&
              !std::filesystem::exists(thumbprintPath),
          "timestamped WinTrust fixture: cleanup leaves no PE, state, or thumbprint file");
    std::error_code error;
    const bool removed = std::filesystem::remove(*fixtureRoot, error) && !error;
    Check(removed, "timestamped WinTrust fixture leaves no temporary directory after cleanup");
}

void TestTimestampedWinTrustWorker(const std::filesystem::path& fixtureRoot) {
    const auto modulePath = CurrentModulePath();
    if (!modulePath.has_value()) {
        Check(false, "timestamped WinTrust worker requires the current test executable path");
        return;
    }
    const std::filesystem::path scriptPath = modulePath->parent_path() / L"AuthenticodeFixture.ps1";
    Check(std::filesystem::exists(scriptPath), "timestamped WinTrust worker helper is deployed beside the test");
    if (!std::filesystem::exists(scriptPath)) {
        return;
    }
    TestRealWinTrustFixture(scriptPath, fixtureRoot, L"TimestampedExpired",
                            "real WinTrust fixture: SignTool creates an RFC3161-timestamped PE", true, true, true,
                            false);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc > 2 && std::wstring(argv[1]) == L"--timestamped-worker") {
        TestTimestampedWinTrustWorker(std::filesystem::path(argv[2]));
        return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"--timestamped") {
        TestTimestampedWinTrustFixture();
        return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    TestExpiredWinTrustFixture();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
