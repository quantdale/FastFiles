#include "ClientAuthentication.h"

#include <windows.h>
#include <securitybaseapi.h>

#include <iterator>

#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/GroupSetup.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/PinnedSignatures.h"

namespace ffindexsvc {

namespace {

std::optional<std::wstring> GetProcessImagePath(HANDLE processHandle) {
    wchar_t buffer[MAX_PATH * 4];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (!QueryFullProcessImageNameW(processHandle, 0, buffer, &size)) {
        return std::nullopt;
    }
    return std::wstring(buffer, size);
}

HANDLE OpenImpersonatedClientProcess(HANDLE pipeHandle, ULONG clientPid) noexcept {
    // Impersonate the pipe client so the client process is queried under
    // that client's own token: its own process DACL always permits
    // PROCESS_QUERY_LIMITED_INFORMATION. This keeps the query scoped to
    // what the client itself may open regardless of the service identity,
    // then immediately revert.
    if (!ImpersonateNamedPipeClient(pipeHandle)) {
        return nullptr;
    }
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientPid);
    RevertToSelf();
    return processHandle;
}

// Case-insensitive check that `path`'s directory is exactly `installDir`
// (after canonicalizing both) and its filename matches `expectedExeName`.
// A prefix-only check (e.g. "C:\FastFiles" matching "C:\FastFilesEvil...")
// would be a bypass, so this requires an exact directory match.
bool IsExpectedInstalledBinary(const std::wstring& path, const std::wstring& installDir, const wchar_t* expectedExeName) {
    wchar_t canonicalPath[MAX_PATH * 4];
    wchar_t canonicalInstallDir[MAX_PATH * 4];
    if (GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(canonicalPath)), canonicalPath, nullptr) == 0) {
        return false;
    }
    if (GetFullPathNameW(installDir.c_str(), static_cast<DWORD>(std::size(canonicalInstallDir)), canonicalInstallDir, nullptr) == 0) {
        return false;
    }

    std::wstring fullPath(canonicalPath);
    std::wstring dir(canonicalInstallDir);
    if (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }

    const size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return false;
    }
    const std::wstring fileDir = fullPath.substr(0, lastSlash);
    const std::wstring fileName = fullPath.substr(lastSlash + 1);

    return _wcsicmp(fileDir.c_str(), dir.c_str()) == 0 && _wcsicmp(fileName.c_str(), expectedExeName) == 0;
}

std::optional<std::vector<uint8_t>> GetImpersonatedClientUserSid(HANDLE pipeHandle) {
    if (!ImpersonateNamedPipeClient(pipeHandle)) {
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> result;
    HANDLE threadToken = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &threadToken)) {
        DWORD needed = 0;
        GetTokenInformation(threadToken, TokenUser, nullptr, 0, &needed);
        if (needed > 0) {
            std::vector<uint8_t> tokenUserBuffer(needed);
            if (GetTokenInformation(threadToken, TokenUser, tokenUserBuffer.data(), needed, &needed)) {
                const TOKEN_USER* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenUserBuffer.data());
                const DWORD sidLength = GetLengthSid(tokenUser->User.Sid);
                std::vector<uint8_t> sidCopy(sidLength);
                if (CopySid(sidLength, sidCopy.data(), tokenUser->User.Sid)) {
                    result = std::move(sidCopy);
                }
            }
        }
        CloseHandle(threadToken);
    }

    RevertToSelf();
    return result;
}

} // namespace

void CloseClientIdentity(ClientIdentity& identity) noexcept {
    if (identity.processHandle != nullptr) {
        CloseHandle(identity.processHandle);
        identity.processHandle = nullptr;
    }
    identity.userSidBuffer.clear();
}

std::optional<ClientIdentity> VerifyClientAtHandshake(
    HANDLE pipeHandle, const std::wstring& installDir,
    ffprotocol::HandshakeRejectReason& outRejectReasonIfFailed) noexcept {
    ULONG clientPid = 0;
    if (!GetNamedPipeClientProcessId(pipeHandle, &clientPid)) {
        outRejectReasonIfFailed = ffprotocol::HandshakeRejectReason::UnverifiedImagePath;
        return std::nullopt;
    }

    HANDLE processHandle = OpenImpersonatedClientProcess(pipeHandle, clientPid);
    if (processHandle == nullptr) {
        outRejectReasonIfFailed = ffprotocol::HandshakeRejectReason::UnverifiedImagePath;
        return std::nullopt;
    }

    auto imagePath = GetProcessImagePath(processHandle);
    if (!imagePath || !IsExpectedInstalledBinary(*imagePath, installDir, ffsetup::kEngineExeName)) {
        CloseHandle(processHandle);
        outRejectReasonIfFailed = ffprotocol::HandshakeRejectReason::UnverifiedImagePath;
        return std::nullopt;
    }

    if (!ffsetup::VerifyPinnedSignature(*imagePath, ffsetup::kExpectedEngineSignatureThumbprint)) {
        CloseHandle(processHandle);
        outRejectReasonIfFailed = ffprotocol::HandshakeRejectReason::UnverifiedSignature;
        return std::nullopt;
    }

    auto userSid = GetImpersonatedClientUserSid(pipeHandle);
    if (!userSid || !ffsetup::IsUserInAuthorizedClientGroup(
        const_cast<PSID>(static_cast<const void*>(userSid->data())))) {
        CloseHandle(processHandle);
        outRejectReasonIfFailed = ffprotocol::HandshakeRejectReason::NotAuthorizedGroup;
        return std::nullopt;
    }

    ClientIdentity identity;
    identity.processHandle = processHandle;
    identity.userSidBuffer = std::move(*userSid);
    return identity;
}

bool RevalidateClient(const ClientIdentity& identity, const std::wstring& installDir) noexcept {
    if (identity.processHandle == nullptr) {
        return false;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(identity.processHandle, &exitCode) || exitCode != STILL_ACTIVE) {
        return false; // process exited; its handle/PID could even be reused by an unrelated process
    }

    auto imagePath = GetProcessImagePath(identity.processHandle);
    if (!imagePath || !IsExpectedInstalledBinary(*imagePath, installDir, ffsetup::kEngineExeName)) {
        return false;
    }
    if (!ffsetup::VerifyPinnedSignature(*imagePath, ffsetup::kExpectedEngineSignatureThumbprint)) {
        return false;
    }
    if (!ffsetup::IsUserInAuthorizedClientGroup(identity.UserSid())) {
        return false; // spec "Revoked membership closes an active connection"
    }
    return true;
}

} // namespace ffindexsvc
