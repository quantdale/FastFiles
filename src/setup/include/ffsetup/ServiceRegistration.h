#pragma once
#include <windows.h>
#include <optional>
#include <string>

#include "ffsetup/SetupResult.h"

namespace ffsetup {

// Service identity selection for FastFilesIndexSvc (resolve-raw-volume-
// privilege-insufficiency tasks 2.1/2.2). The privilege candidate matrix
// (see evidence/matrix-execution-and-selection.md in that change) proved that
// no documented narrow user right or group membership opens a raw NTFS volume
// device; the selected production model is the constrained privileged broker
// running as LocalSystem with the existing narrow closed command surface.
// The virtual-account model remains available as the legacy/dev model and for
// environments where raw-volume scanning is not the active path.
enum class ServiceAccountType {
    VirtualAccount,  // NT SERVICE\FastFilesIndexSvc (legacy dev model)
    LocalSystem,     // selected constrained-broker model
    NetworkService,  // NT AUTHORITY\NetworkService
    LocalService,    // NT AUTHORITY\LocalService
    NamedUser,       // userName (name/password managed by SCM)
};

struct ServiceAccountOptions {
    ServiceAccountType type = ServiceAccountType::LocalSystem;
    std::wstring userName;  // used only when type == NamedUser
};

// Registers FastFilesIndexSvc with the Service Control Manager under the
// selected service identity and grants that account SeBackupPrivilege (the
// raw-volume backup-semantics right). Also configures SCM failure actions
// (task 3.10) and the SCM object's own security descriptor so the authorized
// client group gets query-only rights (task 3.11; spec "No Client-Grantable
// Service Control Rights").
//
// Registration is transactional (task 2.2 / spec "Grant failure prevents an
// invalid service start"): any failure after service creation deletes the
// service AND revokes the rights just granted, so a failed install never
// leaves a half-configured service or orphaned privilege grant behind.
//
// binaryPath must be the fully-qualified, quoted-if-necessary path to
// FastFilesIndexSvc.exe under the ACL-locked install directory.
// clientGroupSid is the authorized client group's SID (see
// SecurityDescriptors.h / GroupSetup.h) -- the group must already exist.
SetupResult RegisterIndexService(const std::wstring& binaryPath, PSID clientGroupSid,
                                 const ServiceAccountOptions& options = ServiceAccountOptions{}) noexcept;

// Reverses RegisterIndexService: stops the service if running, deletes it
// from the SCM, and revokes SeBackupPrivilege from the account the service
// was running under (task 6.4 uninstall path).
SetupResult UnregisterIndexService() noexcept;

// Re-applies the SCM object security descriptor and failure-action
// configuration to an already-registered service without recreating it
// (task 6.3: "reapply ... ACLs on every upgrade, not only first install").
SetupResult ReapplyIndexServiceSecurity(PSID clientGroupSid) noexcept;

// Resolves the SCM start name (lpServiceStartName) for the selected account
// options: nullptr-equivalent "LocalSystem" for LocalSystem, the virtual
// account name, the built-in service account names, or the named user.
// Returns an empty string when the options are invalid.
std::wstring ResolveServiceStartName(const ServiceAccountOptions& options) noexcept;

// Grants SeBackupPrivilege to the account selected by options. For
// LocalSystem the grant is intentionally skipped (SYSTEM inherently holds
// every privilege; the SID-based grant is a no-op that cannot narrow it).
SetupResult GrantServiceAccountPrivilege(const ServiceAccountOptions& options) noexcept;

// Revokes SeBackupPrivilege from the account selected by options. Missing
// accounts or already-revoked rights are treated as success (idempotent
// teardown for rollback and uninstall paths).
SetupResult RevokeServiceAccountPrivilege(const ServiceAccountOptions& options) noexcept;

// Queries the live SCM configuration for the account the index service is
// currently registered under, or nullopt when the service is not registered
// (or its configuration cannot be read). Used by the installer to capture
// prior configuration before a transactional upgrade (task 3.2) and by
// uninstall to revoke the rights of the *actual* configured account rather
// than the default (task 2.1).
std::optional<ServiceAccountOptions> QueryIndexServiceAccount() noexcept;

} // namespace ffsetup
