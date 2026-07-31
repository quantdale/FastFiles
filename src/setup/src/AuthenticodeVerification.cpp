#include "ffsetup/AuthenticodeVerification.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

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

std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprint(const std::wstring& filePath) noexcept {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath.c_str();

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

bool VerifyPinnedSignature(const std::wstring& filePath, const Thumbprint& expected) noexcept {
    if (IsPlaceholderThumbprint(expected)) {
        return false; // fail closed: pin not yet configured
    }
    auto actual = VerifyAuthenticodeAndGetThumbprint(filePath);
    return actual.has_value() && *actual == expected;
}

} // namespace ffsetup
