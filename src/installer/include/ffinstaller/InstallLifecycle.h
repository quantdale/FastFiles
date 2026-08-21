#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "ffsetup/Identifiers.h"

namespace ffinstaller {

// Pure decision logic for the install/uninstall lifecycle (process
// quiescence, self-replacement, rollback bookkeeping, startup
// verification). Kept header-only and side-effect free so tests/setup can
// exercise the state machines without elevation, services, or real
// installs; the Win32 mechanics live in InstallSteps.cpp.

// --- Single-instance guard -------------------------------------------------

// Serializes install/uninstall runs: concurrent elevated installs race on
// the SCM registration and the Backup directory.
constexpr wchar_t kInstallMutexName[] = L"Local\\FastFiles.Install.Mutex";

class InstallMutexGuard {
public:
    explicit InstallMutexGuard(const wchar_t* name)
        : mutex_(CreateMutexW(nullptr, FALSE, name)),
          alreadyRunning_(mutex_ == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {}
    ~InstallMutexGuard() {
        if (mutex_ != nullptr) CloseHandle(mutex_);
    }
    InstallMutexGuard(const InstallMutexGuard&) = delete;
    InstallMutexGuard& operator=(const InstallMutexGuard&) = delete;

    bool AlreadyRunning() const { return alreadyRunning_; }

private:
    HANDLE mutex_;
    bool alreadyRunning_;
};

// --- Client-process quiescence ----------------------------------------------

// The UI/engine are per-user processes, not SCM-visible; the installer must
// close them before replacing their images. The UI gets a graceful
// WM_CLOSE window; the engine has no window and goes straight to the
// terminate phase. Both phases are bounded.
constexpr DWORD kClientCloseWindowTimeoutMs = 10000;
constexpr DWORD kClientTerminateTimeoutMs = 5000;

inline std::vector<std::wstring> ClientProcessImageNames() {
    return {ffsetup::kEngineExeName, L"FastFiles.exe"};
}

// --- Staging / self-replacement ----------------------------------------------

// Case-insensitive Win32 path equality used to detect "the payload being
// staged is the very file at the destination" (self-upgrade from the
// installed FastFilesSetup.exe, or any stage run with source == install
// dir). Such a destination must be renamed aside before promotion, because
// MOVEFILE_REPLACE_EXISTING onto a running/mapped image fails with a
// sharing violation.
inline bool SameFileLocation(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

// --- Fresh-install rollback bookkeeping ----------------------------------------

// Which artifacts a fresh-install run actually created; only those need
// compensation on failure (a pre-existing group or directory belongs to a
// previous state and must survive).
struct FreshInstallArtifacts {
    bool groupCreatedThisRun = false;
    bool installDirCreatedThisRun = false;
};

struct FreshInstallRollbackPlan {
    bool unregisterService = false;
    bool unregisterTask = false;
    bool unregisterMetadata = false;
    bool resetAcl = false;
    bool deleteBinaries = false;
    bool deleteGroup = false;
    bool removeInstallDir = false;
};

inline FreshInstallRollbackPlan DecideFreshInstallRollback(const FreshInstallArtifacts& artifacts) {
    FreshInstallRollbackPlan plan;
    plan.unregisterService = true;
    plan.unregisterTask = true;
    plan.unregisterMetadata = true;
    plan.resetAcl = true;
    plan.deleteBinaries = true;
    plan.deleteGroup = artifacts.groupCreatedThisRun;
    plan.removeInstallDir = artifacts.installDirCreatedThisRun;
    return plan;
}

// --- Startup verification ------------------------------------------------------

// Verdict over the diagnostic lines written after the service's fresh
// start. A readable log with no startup-token line is now a FAILURE: the
// gate exists to prove the fresh token holds SeBackupPrivilege, and
// silently degrading to "it stayed up for a while" lets a privilege-broken
// install pass. Only an unreadable/absent log file proceeds (the caller
// decides that case -- the verdict cannot distinguish it).
enum class StartupTokenVerdict {
    Passed,
    PrivilegeMissing,
    DiagnosticAbsent,
};

inline StartupTokenVerdict EvaluateStartupTokenLines(const std::vector<std::wstring>& lines) {
    for (const auto& line : lines) {
        if (line.find(L"state=startup-token") != std::wstring::npos) {
            return line.find(L"privilegeEnabled=0") != std::wstring::npos
                       ? StartupTokenVerdict::PrivilegeMissing
                       : StartupTokenVerdict::Passed;
        }
    }
    return StartupTokenVerdict::DiagnosticAbsent;
}

} // namespace ffinstaller
