#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <vector>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// Retained per-connection so periodic re-validation (task 3.5) doesn't
// need to re-impersonate the pipe client -- image path is fixed for the
// life of a process, and the user SID lets us re-check LIVE group
// membership independent of any cached logon token.
struct ClientIdentity {
    HANDLE processHandle = nullptr; // owned; PROCESS_QUERY_LIMITED_INFORMATION
    std::vector<uint8_t> userSidBuffer;

    PSID UserSid() const noexcept {
        return userSidBuffer.empty() ? nullptr : const_cast<PSID>(static_cast<const void*>(userSidBuffer.data()));
    }
};

void CloseClientIdentity(ClientIdentity& identity) noexcept;

// Full verification performed at Handshake (task 3.4): the connecting
// process's image path must resolve under the ACL-locked install
// directory, its Authenticode signature must match the pinned thumbprint
// for FastFilesEngine.exe, and its user must currently be a member of the
// authorized client group. On success, returns an identity the caller must
// retain (and eventually close) for later re-validation.
std::optional<ClientIdentity> VerifyClientAtHandshake(
    HANDLE pipeHandle, const std::wstring& installDir,
    ffprotocol::HandshakeRejectReason& outRejectReasonIfFailed) noexcept;

// Re-checks image path + pinned signature (from the retained process
// handle) and LIVE group membership (from the retained user SID) without
// requiring a fresh impersonation call (task 3.5). Returns false if the
// connection should now be closed.
bool RevalidateClient(const ClientIdentity& identity, const std::wstring& installDir) noexcept;

} // namespace ffindexsvc
