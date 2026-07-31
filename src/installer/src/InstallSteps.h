#pragma once

namespace ffinstaller {

// Task 6.1 (fresh install) and 6.3 (upgrade -- re-registers/re-ACLs an
// already-installed FastFiles rather than failing on "already exists").
// Returns a process exit code (0 = success).
int RunInstall();

// Task 6.4: reverses everything RunInstall does.
int RunUninstall();

} // namespace ffinstaller
