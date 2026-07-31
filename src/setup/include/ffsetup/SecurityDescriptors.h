#pragma once
#include <windows.h>
#include <aclapi.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace ffsetup {

// Owns the raw SID bytes returned by LookupAccountNameW. PSID is not a
// distinct allocation type on Windows -- it just points into this buffer.
class OwnedSid {
public:
    OwnedSid() = default;
    explicit OwnedSid(std::vector<uint8_t> buffer) noexcept : buffer_(std::move(buffer)) {}

    PSID Get() const noexcept {
        return buffer_.empty() ? nullptr : const_cast<PSID>(static_cast<const void*>(buffer_.data()));
    }
    bool Valid() const noexcept { return !buffer_.empty(); }

private:
    std::vector<uint8_t> buffer_;
};

// Resolves an account/group name (e.g. kAuthorizedClientGroupName) to its
// SID. Returns std::nullopt if the account does not exist yet -- e.g. the
// installer has not created the authorized client group.
std::optional<OwnedSid> LookupAccountSid(const wchar_t* accountName) noexcept;

// Well-known SIDs needed on every ACL this library builds.
std::optional<OwnedSid> GetLocalSystemSid() noexcept;
std::optional<OwnedSid> GetBuiltinAdministratorsSid() noexcept;

// A self-contained, heap-owned absolute security descriptor. aclBuffer must
// outlive attributes.lpSecurityDescriptor's use, since the descriptor's
// DACL pointer references aclBuffer's storage directly.
struct OwnedSecurityDescriptor {
    std::vector<uint8_t> descriptorBuffer;
    std::vector<uint8_t> aclBuffer;
    SECURITY_ATTRIBUTES attributes{};
};

// Security descriptor for the Ctrl/Data named pipes: full access for SYSTEM
// and Administrators, connect+read+write only for the authorized client
// group, nobody else -- never a null DACL or Everyone/Authenticated Users
// (spec "Named Pipe Access Control").
std::optional<OwnedSecurityDescriptor> BuildPipeSecurityDescriptor(PSID clientGroupSid) noexcept;

// SCM object security descriptor for FastFilesIndexSvc: full control for
// SYSTEM/Administrators, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG only
// for the client group -- never SERVICE_START, SERVICE_STOP,
// SERVICE_CHANGE_CONFIG, WRITE_DAC, or WRITE_OWNER (spec "No
// Client-Grantable Service Control Rights").
std::optional<OwnedSecurityDescriptor> BuildServiceObjectSecurityDescriptor(PSID clientGroupSid) noexcept;

// Security descriptor for the install directory: write access restricted
// to Administrators/TrustedInstaller, read+execute for the client group
// (design.md D4 DLL/binary hardening; spec references install-dir ACL).
std::optional<OwnedSecurityDescriptor> BuildInstallDirSecurityDescriptor(PSID clientGroupSid) noexcept;

// Security descriptor granting full control to SYSTEM/Administrators only
// -- no other principal, not even the authorized client group. Used for
// the service's own log and crash-dump directories (task 3.12), since
// those can contain sensitive data from a SeBackupPrivilege process.
std::optional<OwnedSecurityDescriptor> BuildAdminOnlySecurityDescriptor() noexcept;

// Security descriptor for FastFilesEngine's same-privilege, UI-facing pipe
// (design.md D3): read+write for SYSTEM and the current process's own
// user only -- this seam isn't the elevation boundary, but it still
// shouldn't accept a null DACL or be reachable by other users on a shared
// machine (design.md Risks: "Cross-user visibility").
std::optional<OwnedSecurityDescriptor> BuildCurrentUserPipeSecurityDescriptor() noexcept;

} // namespace ffsetup
