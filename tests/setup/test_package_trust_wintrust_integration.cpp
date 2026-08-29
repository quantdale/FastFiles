// PRIVILEGED TRUST-STORE INTEGRATION SUITE -- opt-in only.
//
// Proves the final RFC3161-timestamped detached-CMS package-manifest format
// through the native installer verifier against a disposable, non-production
// signer installed into CurrentUser\Root (via PackageTrustFixture.ps1's
// silent CERT_STORE_PROV_SYSTEM_REGISTRY write).
//
// This is not part of ordinary FastFiles development; see AGENTS.md "Normal
// FastFiles development". It is never part of the default CTest suite: it is
// registered only when FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS is explicitly
// turned ON (see tests/setup/CMakeLists.txt and
// verify/run_privileged_trust_tests.ps1). Requirements to run this target:
//   - CurrentUser\Root write access (some hardened/hosted images block this
//     store even for a non-elevated user; this suite fails closed rather
//     than fabricating a pass);
//   - `signtool.exe` (Windows SDK) on PATH or under a discovered SDK bin dir;
//   - `pwsh.exe` on PATH.
//
// The certificates this suite generates are disposable, uniquely-named,
// non-production test material created and removed entirely within this
// process's run; they never satisfy release-signing evidence.

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ffsetup/PackageTrust.h"
#include "ffsetup/FastFilesVersion.h"
#include "PackageTrustFixtureSupport.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::wstring FixtureRoot() {
    return (std::filesystem::temp_directory_path() /
            ("FastFiles-package-trust-wintrust-" + std::to_string(GetCurrentProcessId())))
        .wstring();
}

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return L"";
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring();
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool RunPackageTrustFixture(const std::wstring& mode, const std::wstring& packageRoot,
                            const std::wstring& statePath) {
    std::vector<wchar_t> pwshPath(32768);
    const DWORD pwshLength = SearchPathW(nullptr, L"pwsh.exe", nullptr,
                                         static_cast<DWORD>(pwshPath.size()), pwshPath.data(), nullptr);
    const std::wstring fixturePath = Join(ExecutableDirectory(), L"PackageTrustFixture.ps1");
    if (pwshLength == 0 || pwshLength >= pwshPath.size() || fixturePath.empty() ||
        !std::filesystem::is_regular_file(std::filesystem::path(fixturePath))) {
        return false;
    }

    std::wstring command = QuoteCommandLineArgument(std::wstring(pwshPath.data(), pwshLength)) +
                           L" -NoProfile -NonInteractive -File " + QuoteCommandLineArgument(fixturePath) +
                           L" -Mode " + QuoteCommandLineArgument(mode) +
                           L" -PackageRoot " + QuoteCommandLineArgument(packageRoot) +
                           L" -StatePath " + QuoteCommandLineArgument(statePath);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startupInfo, &processInfo)) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 120000);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return false;
    }
    DWORD exitCode = 1;
    const bool exitCodeRead = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return exitCodeRead && exitCode == 0;
}

std::vector<uint8_t> ReadBytes(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::optional<ffsetup::Thumbprint> ReadStateThumbprint(
    const std::wstring& statePath, const char* fieldName) {
    const std::string state = [&] {
        std::ifstream file(std::filesystem::path(statePath), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }();
    const std::string marker = std::string("\"") + fieldName + "\":\"";
    const size_t start = state.find(marker);
    if (start == std::string::npos || start + marker.size() + 40 > state.size()) return std::nullopt;
    const std::string hex = state.substr(start + marker.size(), 40);
    ffsetup::Thumbprint thumbprint{};
    for (size_t index = 0; index < thumbprint.size(); ++index) {
        const char high = hex[index * 2];
        const char low = hex[index * 2 + 1];
        if (!std::isxdigit(static_cast<unsigned char>(high)) ||
            !std::isxdigit(static_cast<unsigned char>(low))) {
            return std::nullopt;
        }
        const auto nibble = [](char value) -> uint8_t {
            if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
            if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
            return static_cast<uint8_t>(value - 'a' + 10);
        };
        thumbprint[index] = static_cast<uint8_t>((nibble(high) << 4) | nibble(low));
    }
    return thumbprint;
}

std::optional<ffsetup::Thumbprint> ReadSignerThumbprint(const std::wstring& statePath) {
    return ReadStateThumbprint(statePath, "leafThumbprint");
}

void TestTimestampedDefaultManifestVerifier(const std::wstring& root) {
    // close-independent-validation-gaps 4.2: prove the final RFC3161 detached
    // CMS format through the native installer verifier and its real trust path.
    const std::wstring timestampRoot = Join(root, L"default-timestamped-manifest");
    const std::wstring statePath = Join(root, L"default-timestamped-manifest.state.json");
    std::filesystem::create_directories(std::filesystem::path(timestampRoot));
    const std::vector<std::wstring> names{L"FastFiles.exe", L"FastFilesEngine.exe",
                                          L"FastFilesIndexSvc.exe", L"FastFilesSetup.exe"};
    for (const auto& name : names) WritePayload(Join(timestampRoot, name), "native-" + AsciiName(name));
    const auto manifest = MakeCanonicalManifest(timestampRoot, names);
    const auto serialized = ffsetup::SerializePackageManifest(manifest);
    Check(serialized.has_value(), "native timestamp fixture has a canonical manifest");
    const std::wstring manifestPath = Join(timestampRoot, ffsetup::kPackageManifestFileName);
    WriteBytes(manifestPath, serialized.value_or(std::vector<uint8_t>{}));
    const auto originalManifest = ReadBytes(manifestPath);

    const bool prepared = RunPackageTrustFixture(L"Prepare", timestampRoot, statePath);
    Check(prepared, "native timestamp fixture prepares a trusted CurrentUser root and detached CMS");
    if (prepared && serialized) {
        Check(ReadBytes(manifestPath) == originalManifest,
              "SignTool timestamping leaves the detached manifest bytes unchanged");
        const auto signer = ReadSignerThumbprint(statePath);
        const std::wstring signaturePath = Join(timestampRoot, ffsetup::kPackageManifestSignatureFileName);
        const auto originalSignature = ReadBytes(signaturePath);
        Check(signer.has_value() && !originalSignature.empty(),
              "native timestamp fixture publishes an exact signer pin and signature bytes");
        if (signer && !originalSignature.empty()) {
            const auto accepted = ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer});
            Check(accepted.has_value() && accepted->signatureVerified,
                  "DefaultManifestSignatureVerifier accepts the timestamped detached CMS");

            auto wrongSigner = *signer;
            wrongSigner[0] ^= 0xFF;
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {wrongSigner}).has_value(),
                  "DefaultManifestSignatureVerifier rejects the wrong manifest signer pin");

            WritePayload(manifestPath, "mutated-after-signing");
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "DefaultManifestSignatureVerifier rejects changed detached manifest bytes");
            WriteBytes(manifestPath, *serialized);

            auto changedSignature = originalSignature;
            changedSignature.back() ^= 0x01;
            WriteBytes(signaturePath, changedSignature);
            Check(!ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "DefaultManifestSignatureVerifier rejects changed timestamp/signature bytes");
            WriteBytes(signaturePath, originalSignature);
            Check(ffsetup::ReadAuthenticatedPackageManifest(timestampRoot, {*signer}).has_value(),
                  "the original timestamped CMS remains valid after negative probes");
        }
    }

    const bool cleaned = RunPackageTrustFixture(L"Cleanup", timestampRoot, statePath);
    Check(cleaned && !std::filesystem::exists(std::filesystem::path(timestampRoot)) &&
              !std::filesystem::exists(std::filesystem::path(statePath)),
          "native timestamp fixture removes CurrentUser trust and every temporary file");
}

} // namespace

int wmain() {
    const std::wstring root = FixtureRoot();
    std::filesystem::remove_all(std::filesystem::path(root));
    std::filesystem::create_directories(std::filesystem::path(root));

    TestTimestampedDefaultManifestVerifier(root);

    std::filesystem::remove_all(std::filesystem::path(root));
    if (FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", FailureCount());
        return 1;
    }
    std::printf("All privileged package-trust WinTrust integration tests passed.\n");
    return 0;
}
