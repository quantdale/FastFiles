#include "ffsetup/SecurityDescriptors.h"

#include <securitybaseapi.h>
#include <sddl.h>

#include "ffsetup/Identifiers.h"

namespace ffsetup {

namespace {

// Builds an absolute SD whose DACL is copied into stable, caller-owned
// storage (aclBuffer) rather than left in the LocalAlloc'd buffer
// SetEntriesInAclW returns -- that buffer is freed before this function
// returns, so the descriptor must not reference it.
std::optional<OwnedSecurityDescriptor> BuildFromExplicitAccess(
    EXPLICIT_ACCESS_W* entries, ULONG entryCount) noexcept {
    PACL rawAcl = nullptr;
    if (SetEntriesInAclW(entryCount, entries, nullptr, &rawAcl) != ERROR_SUCCESS || rawAcl == nullptr) {
        return std::nullopt;
    }

    const ULONG aclSize = static_cast<ULONG>(LocalSize(rawAcl));
    OwnedSecurityDescriptor result;
    result.aclBuffer.assign(reinterpret_cast<uint8_t*>(rawAcl), reinterpret_cast<uint8_t*>(rawAcl) + aclSize);
    LocalFree(rawAcl);

    result.descriptorBuffer.assign(SECURITY_DESCRIPTOR_MIN_LENGTH, 0);
    PSECURITY_DESCRIPTOR sd = result.descriptorBuffer.data();
    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION)) {
        return std::nullopt;
    }
    if (!SetSecurityDescriptorDacl(sd, TRUE, reinterpret_cast<PACL>(result.aclBuffer.data()), FALSE)) {
        return std::nullopt;
    }
    // No SACL, explicit owner/group: inherited from the process token. This
    // is a DACL-only hardening surface -- ownership follows the installer's
    // elevated context, which is the intended trust root.

    result.attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    result.attributes.bInheritHandle = FALSE;
    result.attributes.lpSecurityDescriptor = sd;
    return result;
}

EXPLICIT_ACCESS_W MakeGrant(PSID sid, DWORD accessRights, ACCESS_MODE mode, DWORD inheritance) noexcept {
    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = accessRights;
    ea.grfAccessMode = mode;
    ea.grfInheritance = inheritance;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    return ea;
}

std::optional<OwnedSid> GetWellKnownSid(WELL_KNOWN_SID_TYPE type) noexcept {
    std::vector<uint8_t> buffer(SECURITY_MAX_SID_SIZE);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!CreateWellKnownSid(type, nullptr, buffer.data(), &size)) {
        return std::nullopt;
    }
    buffer.resize(size);
    return OwnedSid(std::move(buffer));
}

} // namespace

std::optional<OwnedSid> LookupAccountSid(const wchar_t* accountName) noexcept {
    std::vector<uint8_t> sidBuffer(SECURITY_MAX_SID_SIZE);
    DWORD sidSize = static_cast<DWORD>(sidBuffer.size());
    wchar_t domainBuffer[256];
    DWORD domainSize = 256;
    SID_NAME_USE use{};

    if (!LookupAccountNameW(nullptr, accountName, sidBuffer.data(), &sidSize, domainBuffer, &domainSize, &use)) {
        return std::nullopt;
    }
    sidBuffer.resize(sidSize);
    return OwnedSid(std::move(sidBuffer));
}

std::optional<OwnedSid> GetLocalSystemSid() noexcept {
    return GetWellKnownSid(WinLocalSystemSid);
}

std::optional<OwnedSid> GetBuiltinAdministratorsSid() noexcept {
    return GetWellKnownSid(WinBuiltinAdministratorsSid);
}

std::optional<OwnedSecurityDescriptor> BuildPipeSecurityDescriptor(PSID clientGroupSid) noexcept {
    auto systemSid = GetLocalSystemSid();
    auto adminSid = GetBuiltinAdministratorsSid();
    if (!systemSid || !adminSid || clientGroupSid == nullptr) {
        return std::nullopt;
    }

    EXPLICIT_ACCESS_W entries[3] = {
        MakeGrant(systemSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, NO_INHERITANCE),
        MakeGrant(adminSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, NO_INHERITANCE),
        MakeGrant(clientGroupSid, FILE_GENERIC_READ | FILE_GENERIC_WRITE, GRANT_ACCESS, NO_INHERITANCE),
    };
    return BuildFromExplicitAccess(entries, 3);
}

std::optional<OwnedSecurityDescriptor> BuildServiceObjectSecurityDescriptor(PSID clientGroupSid) noexcept {
    auto systemSid = GetLocalSystemSid();
    auto adminSid = GetBuiltinAdministratorsSid();
    if (!systemSid || !adminSid || clientGroupSid == nullptr) {
        return std::nullopt;
    }

    EXPLICIT_ACCESS_W entries[3] = {
        MakeGrant(systemSid->Get(), SERVICE_ALL_ACCESS, GRANT_ACCESS, NO_INHERITANCE),
        MakeGrant(adminSid->Get(), SERVICE_ALL_ACCESS, GRANT_ACCESS, NO_INHERITANCE),
        // Deliberately excludes SERVICE_START, SERVICE_STOP,
        // SERVICE_CHANGE_CONFIG, WRITE_DAC, WRITE_OWNER (spec "No
        // Client-Grantable Service Control Rights").
        MakeGrant(clientGroupSid, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG, GRANT_ACCESS, NO_INHERITANCE),
    };
    return BuildFromExplicitAccess(entries, 3);
}

std::optional<OwnedSecurityDescriptor> BuildAdminOnlySecurityDescriptor() noexcept {
    auto systemSid = GetLocalSystemSid();
    auto adminSid = GetBuiltinAdministratorsSid();
    auto serviceSid = LookupAccountSid(kServiceVirtualAccountName);
    if (!systemSid || !adminSid || !serviceSid) {
        return std::nullopt;
    }

    const DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    EXPLICIT_ACCESS_W entries[3] = {
        MakeGrant(systemSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, inherit),
        MakeGrant(adminSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, inherit),
        MakeGrant(serviceSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, inherit),
    };
    return BuildFromExplicitAccess(entries, 3);
}

std::optional<OwnedSecurityDescriptor> BuildCurrentUserPipeSecurityDescriptor() noexcept {
    auto systemSid = GetLocalSystemSid();
    if (!systemSid) {
        return std::nullopt;
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return std::nullopt;
    }
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<uint8_t> tokenUserBuffer(needed);
    const bool ok = needed > 0 && GetTokenInformation(token, TokenUser, tokenUserBuffer.data(), needed, &needed);
    CloseHandle(token);
    if (!ok) {
        return std::nullopt;
    }
    PSID userSid = reinterpret_cast<const TOKEN_USER*>(tokenUserBuffer.data())->User.Sid;

    EXPLICIT_ACCESS_W entries[2] = {
        MakeGrant(systemSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, NO_INHERITANCE),
        MakeGrant(userSid, FILE_GENERIC_READ | FILE_GENERIC_WRITE, GRANT_ACCESS, NO_INHERITANCE),
    };
    return BuildFromExplicitAccess(entries, 2);
}

std::optional<OwnedSecurityDescriptor> BuildInstallDirSecurityDescriptor(PSID clientGroupSid) noexcept {
    auto systemSid = GetLocalSystemSid();
    auto adminSid = GetBuiltinAdministratorsSid();
    auto serviceSid = LookupAccountSid(kServiceVirtualAccountName);
    if (!systemSid || !adminSid || !serviceSid || clientGroupSid == nullptr) {
        return std::nullopt;
    }

    const DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    EXPLICIT_ACCESS_W entries[4] = {
        MakeGrant(systemSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, inherit),
        MakeGrant(adminSid->Get(), FILE_ALL_ACCESS, GRANT_ACCESS, inherit),
        // SCM creates the process under this virtual account, which must
        // be able to load the service image and its adjacent DLLs. It does
        // not receive write access to the protected install directory.
        MakeGrant(serviceSid->Get(), GENERIC_READ | GENERIC_EXECUTE, GRANT_ACCESS, inherit),
        // Admin/TrustedInstaller-write-only: the client group may read and
        // execute the installed binaries but never write into the
        // directory (design.md D4 DLL/binary hardening).
        MakeGrant(clientGroupSid, GENERIC_READ | GENERIC_EXECUTE, GRANT_ACCESS, inherit),
    };
    return BuildFromExplicitAccess(entries, 4);
}

} // namespace ffsetup
