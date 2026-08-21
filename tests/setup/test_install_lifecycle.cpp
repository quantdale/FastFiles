// install-lifecycle decision-logic coverage (audit campaign, installer area):
// pure seams from ffinstaller/InstallLifecycle.h and ffsetup/ServiceStopPolicy.h.
// No elevation, no SCM writes, no process termination -- only the state
// machines behind the installer's stop/await/rename/rollback decisions.
#include "ffinstaller/InstallLifecycle.h"
#include "ffsetup/ServiceStopPolicy.h"

#include "../TestSupport.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

using ffinstaller::EvaluateStartupTokenLines;
using ffinstaller::StartupTokenVerdict;

void TestStartupTokenVerdicts() {
    fftest::Check(EvaluateStartupTokenLines({L"state=startup-token privilegeEnabled=1"}) ==
                      StartupTokenVerdict::Passed,
                  "startup-token line with privilegeEnabled=1 passes");
    fftest::Check(EvaluateStartupTokenLines({L"ts=1 state=startup-token privilegeEnabled=0"}) ==
                      StartupTokenVerdict::PrivilegeMissing,
                  "startup-token line with privilegeEnabled=0 fails");
    fftest::Check(EvaluateStartupTokenLines({L"other=1", L"noise"}) == StartupTokenVerdict::DiagnosticAbsent,
                  "readable log without a startup-token line reports DiagnosticAbsent");
    fftest::Check(EvaluateStartupTokenLines({}) == StartupTokenVerdict::DiagnosticAbsent,
                  "empty log reports DiagnosticAbsent");
    // The failing line must win even when it appears after unrelated lines.
    fftest::Check(EvaluateStartupTokenLines({L"a", L"state=startup-token privilegeEnabled=0", L"b"}) ==
                      StartupTokenVerdict::PrivilegeMissing,
                  "startup-token verdict scans all lines");
}

void TestSameFileLocation() {
    using ffinstaller::SameFileLocation;
    fftest::Check(SameFileLocation(L"C:\\Program Files\\FastFiles\\FastFilesSetup.exe",
                                   L"c:\\PROGRAM FILES\\fastfiles\\FASTFILESSETUP.EXE"),
                  "SameFileLocation is case-insensitive");
    fftest::Check(!SameFileLocation(L"C:\\src\\FastFilesSetup.exe", L"C:\\dest\\FastFilesSetup.exe"),
                  "SameFileLocation distinguishes different directories");
    fftest::Check(!SameFileLocation(L"C:\\a\\FastFiles.exe", L"C:\\a\\FastFilesEngine.exe"),
                  "SameFileLocation distinguishes different file names");
}

void TestFreshInstallRollbackPlan() {
    ffinstaller::FreshInstallRollbackPlan plan =
        ffinstaller::DecideFreshInstallRollback({true, true});
    fftest::Check(plan.unregisterService && plan.unregisterTask && plan.unregisterMetadata &&
                      plan.resetAcl && plan.deleteBinaries,
                  "fresh rollback always undoes service/task/metadata/ACL/binaries");
    fftest::Check(plan.deleteGroup && plan.removeInstallDir,
                  "artifacts created this run are compensated");

    plan = ffinstaller::DecideFreshInstallRollback({false, false});
    fftest::Check(!plan.deleteGroup && !plan.removeInstallDir,
                  "pre-existing group/directory survive a fresh-install rollback");
}

void TestServiceStopPolicy() {
    using namespace ffsetup;
    constexpr ULONGLONG kElapsed = kServiceStopTimeoutMs / 2;
    fftest::Check(ShouldKeepWaitingForServiceStop(SERVICE_STOP_PENDING, kElapsed),
                  "stop-pending within budget keeps waiting");
    fftest::Check(ShouldKeepWaitingForServiceStop(SERVICE_RUNNING, kElapsed),
                  "still-running within budget keeps waiting");
    fftest::Check(!ShouldKeepWaitingForServiceStop(SERVICE_STOPPED, kElapsed),
                  "reaching STOPPED ends the wait");
    fftest::Check(!ShouldKeepWaitingForServiceStop(SERVICE_STOP_PENDING, kServiceStopTimeoutMs + 1),
                  "wait is bounded by the timeout");
    fftest::Check(IsRetryableCreateError(ERROR_SERVICE_MARKED_FOR_DELETE),
                  "ERROR_SERVICE_MARKED_FOR_DELETE is retryable at CreateServiceW");
    fftest::Check(!IsRetryableCreateError(ERROR_ACCESS_DENIED),
                  "unrelated create errors are not retried");
}

void TestClientProcessPlan() {
    const auto names = ffinstaller::ClientProcessImageNames();
    bool hasEngine = false;
    bool hasUi = false;
    for (const auto& name : names) {
        if (_wcsicmp(name.c_str(), ffsetup::kEngineExeName) == 0) hasEngine = true;
        if (_wcsicmp(name.c_str(), L"FastFiles.exe") == 0) hasUi = true;
    }
    fftest::Check(hasEngine && hasUi, "quiescence plan covers both engine and UI images");
}

void TestMutexGuardSerialization() {
    // A second guard on the same Local\ name must detect the first holder --
    // this is exactly the concurrent-installer race the guard exists for.
    // The name is process-unique so concurrent ctest runs cannot collide.
    const std::wstring mutexName =
        L"Local\\FastFiles.Install.Mutex.test." + std::to_wstring(GetCurrentProcessId());
    {
        ffinstaller::InstallMutexGuard first(mutexName.c_str());
        fftest::Check(!first.AlreadyRunning(), "first install-mutex holder acquires cleanly");
        {
            ffinstaller::InstallMutexGuard second(mutexName.c_str());
            fftest::Check(second.AlreadyRunning(), "second install-mutex holder detects the first");
        }
        // The first holder still owns the mutex, so it stays held.
        ffinstaller::InstallMutexGuard third(mutexName.c_str());
        fftest::Check(third.AlreadyRunning(), "mutex stays held while any holder lives");
    }
    // All handles closed: the name is free again.
    ffinstaller::InstallMutexGuard fourth(mutexName.c_str());
    fftest::Check(!fourth.AlreadyRunning(), "mutex is reacquirable after all holders exit");
}

} // namespace

int main() {
    TestStartupTokenVerdicts();
    TestSameFileLocation();
    TestFreshInstallRollbackPlan();
    TestServiceStopPolicy();
    TestClientProcessPlan();
    TestMutexGuardSerialization();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
