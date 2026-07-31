#pragma once
#include <windows.h>
#include <string>

#include "ffsetup/SetupResult.h"

namespace ffsetup {

// Registers FastFilesIndexSvc with the Service Control Manager under its
// virtual service account and grants that account SeBackupPrivilege only
// (task 3.1; design.md D4 "Minimal Privilege Grant"). Also configures SCM
// failure actions (task 3.10) and the SCM object's own security descriptor
// so the authorized client group gets query-only rights (task 3.11; spec
// "No Client-Grantable Service Control Rights").
//
// binaryPath must be the fully-qualified, quoted-if-necessary path to
// FastFilesIndexSvc.exe under the ACL-locked install directory.
// clientGroupSid is the authorized client group's SID (see
// SecurityDescriptors.h / GroupSetup.h) -- the group must already exist.
SetupResult RegisterIndexService(const std::wstring& binaryPath, PSID clientGroupSid) noexcept;

// Reverses RegisterIndexService: stops the service if running, deletes it
// from the SCM, and revokes SeBackupPrivilege from the virtual account
// (task 6.4 uninstall path).
SetupResult UnregisterIndexService() noexcept;

// Re-applies the SCM object security descriptor and failure-action
// configuration to an already-registered service without recreating it
// (task 6.3: "reapply ... ACLs on every upgrade, not only first install").
SetupResult ReapplyIndexServiceSecurity(PSID clientGroupSid) noexcept;

} // namespace ffsetup
