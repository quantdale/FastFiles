#pragma once
#include <string>

#include "ffsetup/SetupResult.h"

namespace ffsetup {

// Applies the install-directory ACL (Admin/TrustedInstaller write-only,
// client-group read+execute) to an already-created directory. Called on
// both first install and every upgrade (task 6.3), not just first install,
// since an upgrade that skips this could leave a stale/looser ACL from an
// older installer version in place.
SetupResult ApplyInstallDirectorySecurity(const std::wstring& installDirPath) noexcept;

// Note on pipe ACLs and task 6.3: the Ctrl/Data named pipes are created
// fresh by FastFilesIndexSvc itself on every service start (see PipeServer
// in the indexsvc target) -- there is no existing pipe object for an
// installer to reapply an ACL to, since named pipes don't outlive the
// service process. The install-directory and SCM object security
// descriptors (this file and ServiceRegistration.h) are the ones that
// persist across an upgrade and must be explicitly reapplied.

} // namespace ffsetup
