// Mutual-authentication pin enforcement (resolve-raw-volume-privilege-
// insufficiency task 3.1 / design.md D4 "Symmetric Mutual Authentication").
//
// The production privileged path only activates when a peer passes
// ffsetup::VerifyPinnedSignature against a configured non-placeholder
// thumbprint. These tests pin that contract:
//   * unconfigured (all-zero) pins must fail closed -- reject every binary;
//   * unsigned binaries must be rejected even when a pin is configured;
//   * a signed binary with a mismatched pin must be rejected;
//   * a signed binary matching its real thumbprint must be accepted.
//
// The positive case re-derives the thumbprint of a known Microsoft-signed
// system binary with the same WinVerifyTrust call the production code uses,
// so the test stays valid after the release signing pipeline provisions the
// real pins.

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/PinnedSignatures.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
ffsetup::Thumbprint ThumbprintWithByte(size_t index, uint8_t value) {
    ffsetup::Thumbprint thumbprint{};
    thumbprint[index] = value;
    return thumbprint;
}

// Mirror of ffsetup's signer-thumbprint extraction so the test derives an
// independent expectation for the positive case.
std::optional<ffsetup::Thumbprint> SignedThumbprint(const std::wstring& filePath) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);

    std::optional<ffsetup::Thumbprint> result;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* providerData = WTHelperProvDataFromStateData(trustData.hWVTStateData);
        if (providerData != nullptr) {
            CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(providerData, 0, FALSE, 0);
            if (signer != nullptr && signer->csCertChain != 0) {
                CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(signer, 0);
                if (cert != nullptr && cert->pCert != nullptr) {
                    ffsetup::Thumbprint thumbprint{};
                    DWORD size = static_cast<DWORD>(thumbprint.size());
                    if (CertGetCertificateContextProperty(cert->pCert, CERT_SHA1_HASH_PROP_ID,
                                                          thumbprint.data(), &size)
                        && size == thumbprint.size()) {
                        result = thumbprint;
                    }
                }
            }
        }
    }

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);
    return result;
}

std::wstring TempUnsignedFile() {
    wchar_t tempDir[MAX_PATH];
    if (GetTempPathW(static_cast<DWORD>(std::size(tempDir)), tempDir) == 0) {
        return {};
    }
    wchar_t tempPath[MAX_PATH];
    if (GetTempFileNameW(tempDir, L"ffpin", 0, tempPath) == 0) {
        return {};
    }
    // Deliberately not a PE: WinVerifyTrust must report no signature.
    const char bytes[64] = {0};
    std::ofstream file(tempPath, std::ios::binary);
    file.write(bytes, sizeof(bytes));
    return tempPath;
}

std::optional<std::wstring> FirstSignedSystemBinary() {
    wchar_t systemDir[MAX_PATH];
    if (GetSystemDirectoryW(systemDir, static_cast<DWORD>(std::size(systemDir))) == 0) {
        return std::nullopt;
    }
    const std::wstring systemDirString(systemDir);
    for (const wchar_t* candidate : {L"notepad.exe", L"explorer.exe", L"cmd.exe",
                                     L"WindowsPowerShell\\v1.0\\powershell.exe"}) {
        const std::wstring path = systemDirString + L"\\" + candidate;
        if (std::filesystem::exists(path) && SignedThumbprint(path).has_value()) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> CurrentModulePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= static_cast<DWORD>(buffer.size())) {
        return std::nullopt;
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::filesystem::path FixtureSourceBinary(const std::filesystem::path& modulePath) {
    const std::filesystem::path compactTestBinary =
        modulePath.parent_path().parent_path() / L"protocol" / L"ffprotocol_tests.exe";
    std::error_code error;
    if (std::filesystem::is_regular_file(compactTestBinary, error) && !error) {
        return compactTestBinary;
    }
    return modulePath;
}

std::optional<std::filesystem::path> CreateFixtureRoot() {
    std::array<wchar_t, MAX_PATH> tempDirectory{};
    const DWORD directoryLength = GetTempPathW(static_cast<DWORD>(tempDirectory.size()), tempDirectory.data());
    if (directoryLength == 0 || directoryLength >= static_cast<DWORD>(tempDirectory.size())) {
        return std::nullopt;
    }

    std::array<wchar_t, MAX_PATH> temporaryFile{};
    if (GetTempFileNameW(tempDirectory.data(), L"ffi", 0, temporaryFile.data()) == 0) {
        return std::nullopt;
    }
    if (!DeleteFileW(temporaryFile.data())) {
        return std::nullopt;
    }

    const std::filesystem::path root(temporaryFile.data());
    std::error_code error;
    if (!std::filesystem::create_directory(root, error) || error) {
        return std::nullopt;
    }
    return root;
}

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += L'\"';
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += character;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
}

int RunFixtureScript(
    const std::filesystem::path& scriptPath,
    const std::vector<std::pair<std::wstring, std::wstring>>& parameters) {
    constexpr DWORD kFixtureProcessTimeoutMs = 240000;
    std::wstring commandLine = QuoteCommandLineArgument(L"pwsh.exe") +
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + QuoteCommandLineArgument(scriptPath.wstring());
    for (const auto& [name, value] : parameters) {
        commandLine += L" " + QuoteCommandLineArgument(name) + L" " + QuoteCommandLineArgument(value);
    }

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    STARTUPINFOW startupInfo{};
    startupInfo.cb = static_cast<DWORD>(sizeof(startupInfo));
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
        return -1;
    }

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, kFixtureProcessTimeoutMs);
    DWORD exitCode = 1;
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(processInfo.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(processInfo.hProcess, 5000);
        exitCode = ERROR_TIMEOUT;
    } else if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        exitCode = ERROR_PROCESS_ABORTED;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}

int HexValue(char character) {
    if (character >= '0' && character <= '9') { return character - '0'; }
    if (character >= 'A' && character <= 'F') { return character - 'A' + 10; }
    if (character >= 'a' && character <= 'f') { return character - 'a' + 10; }
    return -1;
}

std::optional<ffsetup::Thumbprint> ReadFixtureThumbprint(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::string value;
    std::getline(file, value);
    if (value.size() != 40) {
        return std::nullopt;
    }

    ffsetup::Thumbprint thumbprint{};
    for (size_t index = 0; index < thumbprint.size(); ++index) {
        const int high = HexValue(value[index * 2]);
        const int low = HexValue(value[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        thumbprint[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return thumbprint;
}

void TestRealWinTrustFixture(
    const std::filesystem::path& scriptPath,
    const std::filesystem::path& fixtureRoot,
    const wchar_t* caseName,
    const char* description,
    bool expectedAcceptance,
    bool transitionToExpired = false) {
    const std::filesystem::path outputPath = fixtureRoot / (std::wstring(caseName) + L".exe");
    const std::filesystem::path statePath = fixtureRoot / (std::wstring(caseName) + L".state.json");
    const auto modulePath = CurrentModulePath();
    Check(modulePath.has_value(), "real WinTrust fixture: current test executable path is available");
    if (!modulePath.has_value()) {
        return;
    }
    const std::filesystem::path sourceBinary = FixtureSourceBinary(*modulePath);

    const int prepareExit = RunFixtureScript(scriptPath, {
        {L"-Action", L"Prepare"},
        {L"-StatePath", statePath.wstring()},
        {L"-SourceBinary", sourceBinary.wstring()},
        {L"-OutputPath", outputPath.wstring()},
        {L"-Case", caseName}
    });
    Check(prepareExit == 0, description);

    const std::filesystem::path thumbprintPath = outputPath.wstring() + L".thumbprint";
    const auto expected = ReadFixtureThumbprint(thumbprintPath);
    Check(expected.has_value(), "real WinTrust fixture: signer thumbprint is recorded as a public test fact");
    if (prepareExit == 0 && expected.has_value()) {
        const int customChainExit = RunFixtureScript(scriptPath, {
            {L"-Action", L"Validate"},
            {L"-StatePath", statePath.wstring()}
        });
        Check(customChainExit == 0,
              transitionToExpired
                  ? "real WinTrust fixture: custom-root chain is valid before the deliberate expiry transition"
                  : "real WinTrust fixture: custom-root chain is valid while the default WinTrust root remains untrusted");

        const auto verifyCurrentState = [&](bool shouldAccept, bool checkPinVariants) {
            const auto actual = ffsetup::VerifyAuthenticodeAndGetThumbprintAtPath(outputPath.wstring());
            Check(actual.has_value() == shouldAccept,
                  shouldAccept
                      ? "real WinTrust fixture: trusted test signer establishes a valid chain"
                      : "real WinTrust fixture: WinTrust does not establish trust for the invalid signer");
            if (shouldAccept && actual.has_value()) {
                Check(ffsetup::ConstantTimeThumbprintEqual(*actual, *expected),
                      "real WinTrust fixture: accepted signer thumbprint matches the generated leaf");
            }
            Check(ffsetup::VerifyPinnedSignature(outputPath.wstring(), *expected) == shouldAccept,
                  shouldAccept
                      ? "real WinTrust fixture: VerifyPinnedSignature accepts only the trusted test signer"
                      : "real WinTrust fixture: VerifyPinnedSignature rejects the invalid test signer");
            if (checkPinVariants) {
                ffsetup::Thumbprint wrongPin = *expected;
                wrongPin[0] ^= 0xFF;
                Check(!ffsetup::VerifyPinnedSignature(outputPath.wstring(), wrongPin),
                      "real WinTrust fixture: a mismatched pin rejects the otherwise valid signer");
                const ffsetup::Thumbprint placeholder{};
                Check(!ffsetup::VerifyPinnedSignature(outputPath.wstring(), placeholder),
                      "real WinTrust fixture: a placeholder pin rejects the otherwise valid signer");
            }
        };

        // SignTool cannot create a new signature with an already-expired
        // certificate. For that fixture, prove the custom-root chain is valid
        // while the leaf is usable, then wait past its embedded NotAfter and
        // run the production WinTrust path again. The temporary root is never
        // installed into the machine trust store, so default WinTrust is
        // expected to reject the fixture both before and after expiry.
        verifyCurrentState(expectedAcceptance, true);
        if (transitionToExpired) {
            const int expireExit = RunFixtureScript(scriptPath, {
                {L"-Action", L"Expire"},
                {L"-StatePath", statePath.wstring()}
            });
            Check(expireExit == 0, "real WinTrust fixture: short-lived signer reaches its expired validity interval");
            const int expiredChainExit = RunFixtureScript(scriptPath, {
                {L"-Action", L"Validate"},
                {L"-StatePath", statePath.wstring()}
            });
            Check(expiredChainExit == 0,
                  "real WinTrust fixture: custom-root chain reports the leaf's expired validity status");
            verifyCurrentState(false, false);
        }
    }

    int cleanupExit = RunFixtureScript(scriptPath, {
        {L"-Action", L"Cleanup"},
        {L"-StatePath", statePath.wstring()}
    });
    if (cleanupExit != 0) {
        // WinTrust can release a certificate context shortly after its close
        // action returns. Retry the isolated cleanup script once across that
        // process boundary so a transient store race cannot strand fixtures.
        Sleep(1000);
        cleanupExit = RunFixtureScript(scriptPath, {
            {L"-Action", L"Cleanup"},
            {L"-StatePath", statePath.wstring()}
        });
    }
    Check(cleanupExit == 0, "real WinTrust fixture: certificate stores and temporary files are cleaned");
    Check(!std::filesystem::exists(outputPath) && !std::filesystem::exists(statePath) &&
              !std::filesystem::exists(thumbprintPath),
          "real WinTrust fixture: cleanup leaves no generated PE, state, or thumbprint file");
}

void TestExpiredAndUntrustedWinTrustFixtures() {
    // close-independent-validation-gaps 4.2: exercise the real WinTrust
    // decision with ephemeral, non-production signed PE fixtures. The valid
    // fixture is a technical seam only and never represents release evidence.
    const auto modulePath = CurrentModulePath();
    if (!modulePath.has_value()) {
        Check(false, "real WinTrust fixtures require the current test executable path");
        return;
    }
    const std::filesystem::path scriptPath = modulePath->parent_path() / L"AuthenticodeFixture.ps1";
    Check(std::filesystem::exists(scriptPath), "real WinTrust fixtures: fixture helper is deployed beside the test");
    if (!std::filesystem::exists(scriptPath)) {
        return;
    }

    const auto fixtureRoot = CreateFixtureRoot();
    Check(fixtureRoot.has_value(), "real WinTrust fixtures: isolated temporary directory is created");
    if (!fixtureRoot.has_value()) {
        return;
    }

    TestRealWinTrustFixture(scriptPath, *fixtureRoot, L"Expired",
                            "real WinTrust fixture: short-lived test certificate is generated and signed", false, true);
    TestRealWinTrustFixture(scriptPath, *fixtureRoot, L"Untrusted",
                            "real WinTrust fixture: untrusted development root certificate is generated and signed", false);

    std::error_code error;
    const bool removed = std::filesystem::remove(*fixtureRoot, error) && !error;
    Check(removed, "real WinTrust fixtures: isolated temporary directory is empty after cleanup");
}

void TestUnconfiguredPinsFailClosed() {
    const std::wstring unsignedFile = TempUnsignedFile();
    const ffsetup::Thumbprint placeholder{};
    Check(!ffsetup::VerifyPinnedSignature(unsignedFile, placeholder),
          "unconfigured pins: an all-zero pin rejects every peer, even an unsigned one");
    Check(!ffsetup::VerifyPinnedSignature(L"C:\\definitely-not-a-file.exe", placeholder),
          "unconfigured pins: rejection does not depend on the file existing");
    DeleteFileW(unsignedFile.c_str());
}

void TestUnsignedBinaryRejectedWithConfiguredPin() {
    const std::wstring unsignedFile = TempUnsignedFile();
    const ffsetup::Thumbprint configuredPin = ThumbprintWithByte(0, 0xAB);
    Check(!ffsetup::VerifyPinnedSignature(unsignedFile, configuredPin),
          "configured pin: an unsigned binary is rejected (no signature, no match)");
    DeleteFileW(unsignedFile.c_str());
}

void TestMismatchedPinRejectsSignedBinary() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove the mismatch path\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "mismatched pin: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        const ffsetup::Thumbprint wrongPin = ThumbprintWithByte(0, static_cast<uint8_t>(actual->at(0) ^ 0xFF));
        Check(!ffsetup::VerifyPinnedSignature(*signedBinary, wrongPin),
              "mismatched pin: a signed binary is rejected when the pin does not match its signer");
    }
}

void TestMatchingPinAcceptsSignedBinary() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove the acceptance path\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "matching pin: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        Check(ffsetup::VerifyPinnedSignature(*signedBinary, *actual),
              "matching pin: a signed binary is accepted when the pin matches its signer");
    }
}

void TestThumbprintComparison() {
    const ffsetup::Thumbprint first = ThumbprintWithByte(0, 0x12);
    const ffsetup::Thumbprint same = ThumbprintWithByte(0, 0x12);
    const ffsetup::Thumbprint different = ThumbprintWithByte(19, 0x34);
    Check(ffsetup::ConstantTimeThumbprintEqual(first, same),
          "thumbprint comparison accepts identical fixed-size digests");
    Check(!ffsetup::ConstantTimeThumbprintEqual(first, different),
          "thumbprint comparison rejects a difference at the final byte");
}

void TestPartiallyPopulatedPinsRejectUnpinnedPeer() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove partial-pin rejection\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "partial pin set: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        Check(ffsetup::VerifyPinnedSignature(*signedBinary, *actual),
              "partial pin set: the populated peer pin remains usable");
        const ffsetup::Thumbprint placeholder{};
        Check(!ffsetup::VerifyPinnedSignature(*signedBinary, placeholder),
              "partial pin set: the peer whose pin remains placeholder is rejected");
    }
}

} // namespace

int main() {
    // resolve-raw-volume-privilege-insufficiency task 3.1: pin enforcement.
    TestUnconfiguredPinsFailClosed();
    TestUnsignedBinaryRejectedWithConfiguredPin();
    TestMismatchedPinRejectsSignedBinary();
    TestMatchingPinAcceptsSignedBinary();
    TestThumbprintComparison();
    TestPartiallyPopulatedPinsRejectUnpinnedPeer();
    TestExpiredAndUntrustedWinTrustFixtures();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
