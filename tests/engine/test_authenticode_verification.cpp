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
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
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

} // namespace

int main() {
    // resolve-raw-volume-privilege-insufficiency task 3.1: pin enforcement.
    TestUnconfiguredPinsFailClosed();
    TestUnsignedBinaryRejectedWithConfiguredPin();
    TestMismatchedPinRejectsSignedBinary();
    TestMatchingPinAcceptsSignedBinary();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
