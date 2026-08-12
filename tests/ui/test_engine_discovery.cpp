// recovery-degraded-browsing 7.1: engine discovery candidates are derived
// deterministically from the UI executable's own location and the fixed
// install location -- never a PATH lookup or a disk-wide search -- and only
// existing regular files are launchable.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

#include "EngineDiscovery.h"

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool ContainsCandidate(const std::vector<std::wstring>& candidates, const wchar_t* expected) {
    for (const auto& candidate : candidates) {
        if (_wcsicmp(candidate.c_str(), expected) == 0) {
            return true;
        }
    }
    return false;
}

int CountCandidate(const std::vector<std::wstring>& candidates, const wchar_t* expected) {
    int count = 0;
    for (const auto& candidate : candidates) {
        if (_wcsicmp(candidate.c_str(), expected) == 0) {
            ++count;
        }
    }
    return count;
}

std::wstring UnsignedFixture() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory) == 0) {
        return {};
    }
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempFileNameW(tempDirectory, L"ffdisc", 0, tempPath) == 0) {
        return {};
    }
    const char bytes[64] = {};
    std::ofstream file(std::filesystem::path(tempPath), std::ios::binary | std::ios::trunc);
    file.write(bytes, sizeof(bytes));
    return tempPath;
}

void TestReparsePathRejected() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory) == 0) {
        std::printf("note: temporary path unavailable; reparse-path check skipped\n");
        return;
    }
    const std::filesystem::path root = std::filesystem::path(tempDirectory) /
        (L"FastFiles-discovery-reparse-" + std::to_wstring(GetCurrentProcessId()));
    const std::filesystem::path target = root / L"target";
    const std::filesystem::path link = root / L"link";
    const std::filesystem::path targetExecutable = target / L"FastFilesEngine.exe";
    std::error_code error;
    std::filesystem::create_directories(target, error);
    if (error) {
        std::printf("note: reparse fixture directory could not be created; check skipped\n");
        return;
    }
    std::ofstream file(targetExecutable, std::ios::binary);
    const char bytes[32] = {};
    file.write(bytes, sizeof(bytes));
    file.close();

    const DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != 0) {
        const auto validation = ffui::ValidateEngineExecutable((link / L"FastFilesEngine.exe").wstring());
        Check(validation.status == ffui::EngineExecutableValidationStatus::CanonicalPathRejected,
              "a symlinked install path is rejected before signature validation");
        RemoveDirectoryW(link.c_str());
    } else {
        std::printf("note: symbolic-link creation unavailable; reparse-path check skipped\n");
    }
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    // 1. Installed layout: the UI's own directory is the first candidate.
    const auto installed =
        ffui::EngineDiscoveryCandidates(L"C:\\Program Files\\FastFiles\\FastFiles.exe");
    Check(!installed.empty(), "installed layout yields candidates");
    Check(_wcsicmp(installed[0].c_str(), L"C:\\Program Files\\FastFiles\\FastFilesEngine.exe") == 0,
          "same-directory candidate is first");

    // 2. Development layout: sibling "engine" directory of the UI's parent,
    //    normalized (build/<config>/src/ui -> build/<config>/src/engine).
    const auto dev = ffui::EngineDiscoveryCandidates(L"D:\\build\\debug\\src\\ui\\FastFiles.exe");
    Check(ContainsCandidate(dev, L"D:\\build\\debug\\src\\engine\\FastFilesEngine.exe"),
          "dev sibling engine candidate present");
    Check(ContainsCandidate(dev, L"D:\\build\\debug\\src\\ui\\FastFilesEngine.exe"),
          "dev same-dir candidate present");

    // 3. Installer layout: %ProgramFiles%\FastFiles is always a candidate.
    wchar_t programFiles[MAX_PATH];
    const DWORD programFilesLength =
        GetEnvironmentVariableW(L"ProgramFiles", programFiles, static_cast<DWORD>(std::size(programFiles)));
    if (programFilesLength != 0) {
        const std::wstring expected = std::wstring(programFiles) + L"\\FastFiles\\FastFilesEngine.exe";
        Check(ContainsCandidate(dev, expected.c_str()), "ProgramFiles install candidate present");
        // Overlapping candidates are deduplicated case-insensitively.
        const int duplicates = CountCandidate(dev, expected.c_str());
        Check(duplicates == 1, "no duplicate install candidate");
    } else {
        std::printf("note: ProgramFiles not resolvable; install-layout checks skipped\n");
    }

    // 4. The dev sibling candidate must not contain an unresolved "..".
    for (const auto& candidate : dev) {
        Check(candidate.find(L"\\..\\") == std::wstring::npos, "candidates are normalized");
    }

    // 5. Validation: only existing regular files pass.
    Check(!ffui::IsValidEngineExecutable(L"C:\\Windows"), "directory rejected");
    Check(!ffui::IsValidEngineExecutable(L"C:\\definitely-not-a-real-file-xyz.exe"), "missing file rejected");
    wchar_t ownPath[MAX_PATH * 4];
    const DWORD ownLength = GetModuleFileNameW(nullptr, ownPath, static_cast<DWORD>(std::size(ownPath)));
    Check(ownLength != 0 && ffui::IsValidEngineExecutable(ownPath), "test executable itself validates");

    // Authenticated launch validation must reject a regular unsigned file in
    // the normal build, while retaining the explicit diagnostic-only escape
    // hatch for disposable local development runs.
    const std::wstring unsignedFixture = UnsignedFixture();
    Check(!unsignedFixture.empty(), "unsigned discovery fixture is created");
    if (!unsignedFixture.empty()) {
        const ffui::EngineExecutableValidation validation =
            ffui::ValidateEngineExecutable(unsignedFixture);
        Check(!validation.canonicalPath.empty(), "canonical path is resolved before signature validation");
#ifdef FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED
        Check(validation.Accepted(), "diagnostic override accepts the unsigned discovery fixture");
#else
        Check(validation.status == ffui::EngineExecutableValidationStatus::Unsigned,
              "unsigned discovery fixture is rejected before launch");
#endif
        DeleteFileW(unsignedFixture.c_str());
    }
    TestReparsePathRejected();

    if (FailureCount() != 0) {
        std::fprintf(stderr, "%d failure(s)\n", FailureCount());
        return 1;
    }
    std::printf("ffengine_discovery_tests: all checks passed\n");
    return 0;
}
