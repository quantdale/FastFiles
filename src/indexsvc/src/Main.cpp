// FastFilesIndexSvc: the privileged, stateless Windows Service.
//
// Ships as a protocol/security skeleton in this change: Handshake, mutual
// authentication, volume enumeration, and connection-scoped bookkeeping
// are fully implemented; StartVolumeScan/OpenUsnJournal reply with an
// explicit "not yet implemented" status rather than performing real
// MFT/USN parsing, which lands in a follow-up change. See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <cstdio>
#include <iterator>
#include <string>

#include "ffipc/PipeListener.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/SecurityDescriptors.h"

#include "ConnectionRegistry.h"
#include "DiagnosticsHardening.h"
#include "DllHardening.h"
#include "PrivilegeVerification.h"
#include "ServiceConnection.h"
#include "StalenessMonitor.h"

namespace {

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;

ffindexsvc::ConnectionRegistry g_registry;
ffipc::PipeListener g_ctrlListener;
ffipc::PipeListener g_dataListener;
ffsetup::OwnedSecurityDescriptor g_pipeSecurityDescriptor;

std::wstring GetOwnDirectory() {
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0) {
        return L"";
    }
    std::wstring fullPath(path, length);
    const size_t lastSlash = fullPath.find_last_of(L"\\/");
    return lastSlash == std::wstring::npos ? L"" : fullPath.substr(0, lastSlash);
}

void SetStatus(DWORD state, DWORD exitCode = NO_ERROR) {
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    SetServiceStatus(g_statusHandle, &g_status);
}

void StopEverything() {
    g_ctrlListener.Stop();
    g_dataListener.Stop();
    ffindexsvc::StopStalenessMonitor();
}

void WINAPI ServiceCtrlHandler(DWORD control) {
    if (control == SERVICE_CONTROL_STOP) {
        SetStatus(SERVICE_STOP_PENDING);
        ffindexsvc::WriteServiceLifecycleLog(L"service stop requested");
        StopEverything();
        ffindexsvc::WriteServiceLifecycleLog(L"service stopped");
        SetStatus(SERVICE_STOPPED);
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerW(ffsetup::kServiceName, ServiceCtrlHandler);
    if (g_statusHandle == nullptr) {
        return;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwControlsAccepted = 0;
    SetStatus(SERVICE_START_PENDING);

    // Task 3.3: before any LoadLibrary call in this process.
    ffindexsvc::HardenDllSearchPath();

    // Task 3.12: log/crash-dump path hardening.
    ffindexsvc::EnsureAdminOnlyLogDirectory();
    ffindexsvc::HardenCrashDumpHandling();

    // Task 3.9: capture the on-disk hash before starting the periodic
    // comparison timer.
    ffindexsvc::CaptureLoadedBinaryHash();
    ffindexsvc::StartStalenessMonitor();

    // Task 7.1: self-check on every real start, not a one-off dev-machine
    // tool -- see PrivilegeVerification.h for why.
    ffindexsvc::VerifyBackupPrivilegeSufficiency();

    const std::wstring installDir = GetOwnDirectory();

    auto clientGroupSid = ffsetup::LookupAccountSid(ffsetup::kAuthorizedClientGroupName);
    if (!clientGroupSid) {
        // The authorized client group must exist (created by the
        // installer) before the pipes can be correctly secured -- fail
        // closed rather than fall back to a weaker ACL.
        std::fwprintf(stderr, L"FastFilesIndexSvc: authorized client group '%ls' not found -- refusing to start\n",
                       ffsetup::kAuthorizedClientGroupName);
        SetStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    auto pipeSecurityDescriptor = ffsetup::BuildPipeSecurityDescriptor(clientGroupSid->Get());
    if (!pipeSecurityDescriptor) {
        std::fwprintf(stderr, L"FastFilesIndexSvc: failed to build pipe security descriptor -- refusing to start\n");
        SetStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }
    g_pipeSecurityDescriptor = std::move(*pipeSecurityDescriptor);

    const bool ctrlStarted = g_ctrlListener.Start(
        ffsetup::kCtrlPipeName, &g_pipeSecurityDescriptor.attributes,
        [installDir](HANDLE pipeHandle) { ffindexsvc::RunCtrlConnection(pipeHandle, installDir, g_registry); });

    const bool dataStarted = g_dataListener.Start(
        ffsetup::kDataPipeName, &g_pipeSecurityDescriptor.attributes,
        [](HANDLE pipeHandle) { ffindexsvc::RunDataConnection(pipeHandle); });

    if (!ctrlStarted || !dataStarted) {
        StopEverything();
        SetStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetStatus(SERVICE_RUNNING);
    ffindexsvc::WriteServiceLifecycleLog(L"service started");
}

} // namespace

int wmain() {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(ffsetup::kServiceName), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}
