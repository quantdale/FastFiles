#include "ffsetup/AuthenticodeVerification.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace ffsetup {

namespace {

std::optional<Thumbprint> ExtractSignerThumbprint(CRYPT_PROVIDER_DATA* providerData) noexcept {
    if (providerData == nullptr) {
        return std::nullopt;
    }
    CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(providerData, 0, FALSE, 0);
    if (signer == nullptr || signer->csCertChain == 0) {
        return std::nullopt;
    }
    CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(signer, 0);
    if (cert == nullptr || cert->pCert == nullptr) {
        return std::nullopt;
    }

    Thumbprint thumbprint{};
    DWORD size = static_cast<DWORD>(thumbprint.size());
    if (!CertGetCertificateContextProperty(cert->pCert, CERT_SHA1_HASH_PROP_ID, thumbprint.data(), &size)
        || size != thumbprint.size()) {
        return std::nullopt;
    }
    return thumbprint;
}

} // namespace

std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprint(
    HANDLE fileHandle, const std::wstring& canonicalPath) noexcept {
    if (fileHandle == nullptr || fileHandle == INVALID_HANDLE_VALUE || canonicalPath.empty()) {
        return std::nullopt;
    }
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = canonicalPath.c_str();
    // Microsoft documents pcwszFilePath as required and hFile as the
    // optional open file to verify. Supplying both is intentional: the path
    // is diagnostic/policy context, while hFile binds the trust decision to
    // the same object whose bytes the installer will consume.
    fileInfo.hFile = fileHandle;

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE; // no network dependency for a local security decision
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);

    std::optional<Thumbprint> result;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* providerData = WTHelperProvDataFromStateData(trustData.hWVTStateData);
        result = ExtractSignerThumbprint(providerData);
    }

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);

    return result;
}

std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprintAtPath(
    const std::wstring& filePath) noexcept {
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    const bool regular = GetFileInformationByHandle(file, &information) != FALSE &&
                         (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                         (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    if (!regular) {
        CloseHandle(file);
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(512);
    std::optional<std::wstring> canonicalPath;
    for (size_t attempt = 0; attempt < 4; ++attempt) {
        const DWORD length = GetFinalPathNameByHandleW(file, buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       FILE_NAME_NORMALIZED);
        if (length == 0 || length >= 32768) {
            break;
        }
        if (length < buffer.size()) {
            canonicalPath = std::wstring(buffer.data(), length);
            break;
        }
        buffer.resize(static_cast<size_t>(length) + 1);
    }

    const auto result = canonicalPath
        ? VerifyAuthenticodeAndGetThumbprint(file, *canonicalPath)
        : std::optional<Thumbprint>{};
    CloseHandle(file);
    return result;
}

bool VerifyPinnedSignature(const std::wstring& filePath, const Thumbprint& expected) noexcept {
#ifdef FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED
    // Compile-time-only escape hatch for a disposable local diagnostic build.
    // Never enable this in a shipped binary: production mutual authentication
    // must continue to fail closed when pins are not configured.
    (void)filePath;
    (void)expected;
    return true;
#else
    if (IsPlaceholderThumbprint(expected)) {
        return false; // fail closed: pin not yet configured
    }
    auto actual = VerifyAuthenticodeAndGetThumbprintAtPath(filePath);
    return actual.has_value() && ConstantTimeThumbprintEqual(*actual, expected);
#endif
}

} // namespace ffsetup
