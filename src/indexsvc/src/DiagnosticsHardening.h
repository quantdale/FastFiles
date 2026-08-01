#pragma once
#include <string>

namespace ffindexsvc {

// Creates (if needed) and ACLs the service's log directory to
// admin-only read/write, and returns its path for the logger to use
// (task 3.12).
std::wstring EnsureAdminOnlyLogDirectory();

// Appends a timestamped lifecycle record to the admin-only service log.
// Logging is best-effort and never weakens the service's fail-closed paths.
void WriteServiceLifecycleLog(const wchar_t* message);

// Redirects Windows Error Reporting local crash dumps for this binary away
// from the default per-user location (%LOCALAPPDATA%\CrashDumps, which is
// user-readable) to an admin-only directory, and additionally excludes
// this binary from WER entirely as the primary mitigation -- a
// SeBackupPrivilege process's crash dump can contain raw filesystem
// metadata that shouldn't be user-readable by default (task 3.12).
void HardenCrashDumpHandling();

} // namespace ffindexsvc
