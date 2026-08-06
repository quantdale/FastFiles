// FastFilesIndexSvc: the privileged, stateless Windows Service.
//
// Runs as a constrained privileged broker under LocalSystem (see
// openspec/changes/resolve-raw-volume-privilege-insufficiency/evidence/
// matrix-execution-and-selection.md). The full protocol surface is
// implemented: Handshake, mutual authentication, volume enumeration,
// connection-scoped bookkeeping, and real MFT/USN scan/journal streaming
// (VolumeScanner / UsnJournalReader / MftParser, wired into
// ServiceConnection). See openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <sstream>
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
ffsetup::OwnedSecurityDescriptor g_pipeSecurityDescriptor;

// Saved command-line arguments so ServiceMain can detect --run-candidate-matrix
// when the SCM starts the process. ServiceMain receives only the service start
// parameters (empty for sc.exe start), not the ImagePath command-line arguments.
int g_argc = 0;
wchar_t** g_argv = nullptr;

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

// Helpers for the diagnostic candidate-matrix mode (resolve-raw-volume-
// privilege-insufficiency task 1.3). In matrix mode the binary is started by
// the SCM under a reconfigured service token, captures one candidate row,
// writes the evidence to a fixed JSON file, and exits. A runner script stops
// the service, applies each candidate configuration, restarts the binary with
// these arguments, collects the evidence, and restores the original config.

std::wstring EscapeJsonString(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
            case L'\\': result += L"\\\\"; break;
            case L'"': result += L"\\\""; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

bool WriteMatrixRowJson(const std::wstring& path, const ffindexsvc::CandidateMatrixRow& row) {
    std::wostringstream json;
    json << L"{\n";
    json << L"  \"candidateId\": \"" << EscapeJsonString(row.candidateId) << L"\",\n";
    json << L"  \"privilegeName\": \"" << EscapeJsonString(row.privilegeName) << L"\",\n";
    json << L"  \"volumePath\": \"" << EscapeJsonString(row.volumePath) << L"\",\n";
    json << L"  \"accountName\": \"" << EscapeJsonString(row.tokenState.accountName) << L"\",\n";
    json << L"  \"accountSid\": \"" << EscapeJsonString(row.tokenState.accountSid) << L"\",\n";
    json << L"  \"groupContext\": \"" << EscapeJsonString(ffindexsvc::FormatGroupContext(row.tokenState.groups)) << L"\",\n";
    json << L"  \"privilegeHeld\": " << (row.privilegeHeld ? L"true" : L"false") << L",\n";
    json << L"  \"privilegeEnabled\": " << (row.privilegeEnabled ? L"true" : L"false") << L",\n";
    json << L"  \"volumeOpened\": " << (row.volumeOpened ? L"true" : L"false") << L",\n";
    json << L"  \"volumeOpenError\": " << row.volumeOpenError << L",\n";
    json << L"  \"journalQueried\": " << (row.journalQueried ? L"true" : L"false") << L",\n";
    json << L"  \"journalQueryError\": " << row.journalQueryError << L",\n";
    json << L"  \"journalRead\": " << (row.journalRead ? L"true" : L"false") << L",\n";
    json << L"  \"journalReadError\": " << row.journalReadError << L",\n";
    json << L"  \"registrationOrder\": " << row.registrationOrder << L",\n";
    json << L"  \"outcome\": \"" << ffindexsvc::CandidateMatrixOutcomeName(row.outcome) << L"\"\n";
    json << L"}\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const std::string utf8 = ToUtf8(json.str());
    out.write(utf8.data(), utf8.size());
    return static_cast<bool>(out);
}

// Returns true if the process was invoked in matrix mode. If true, the caller
// must not start the service control dispatcher.
bool RunCandidateMatrix(int argc, wchar_t* argv[]) {
    int matrixIndex = -1;
    int outputIndex = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--run-candidate-matrix") == 0) {
            matrixIndex = i;
        } else if (std::wcscmp(argv[i], L"--matrix-output") == 0) {
            outputIndex = i;
        }
    }
    if (matrixIndex < 0) {
        return false;
    }

    if (matrixIndex + 4 >= argc || (outputIndex >= 0 && outputIndex + 1 >= argc)) {
        std::fwprintf(stderr,
            L"FastFilesIndexSvc: usage: --run-candidate-matrix <candidateId> <privilegeName> <driveLetter> <regOrder> [--matrix-output <path>]\n");
        return true;
    }

    const std::wstring candidateId = argv[matrixIndex + 1];
    const std::wstring privilegeName = argv[matrixIndex + 2];
    const wchar_t driveLetter = argv[matrixIndex + 3][0];
    const uint32_t regOrder = static_cast<uint32_t>(std::wcstoul(argv[matrixIndex + 4], nullptr, 10));

    // Apply the same hardening the normal service path uses, but stay in the
    // matrix-mode code path so we never start listeners or accept clients.
    ffindexsvc::HardenDllSearchPath();
    ffindexsvc::EnsureAdminOnlyLogDirectory();

    const ffindexsvc::CandidateMatrixRow row = ffindexsvc::CaptureCandidateMatrixRow(
        candidateId, privilegeName, driveLetter, regOrder);
    ffindexsvc::LogCandidateMatrixRow(row);

    if (outputIndex >= 0) {
        const std::wstring outputPath = argv[outputIndex + 1];
        if (!WriteMatrixRowJson(outputPath, row)) {
            std::fwprintf(stderr, L"FastFilesIndexSvc: failed to write matrix output to %ls\n", outputPath.c_str());
        }
    }

    std::fwprintf(stderr,
        L"FastFilesIndexSvc: candidate=%ls privilege=%ls outcome=%ls volumeOpenError=0x%lX journalQueryError=0x%lX journalReadError=0x%lX\n",
        candidateId.c_str(), privilegeName.c_str(), ffindexsvc::CandidateMatrixOutcomeName(row.outcome),
        row.volumeOpenError, row.journalQueryError, row.journalReadError);
    return true;
}

void SetStatus(DWORD state, DWORD exitCode = NO_ERROR) {
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    SetServiceStatus(g_statusHandle, &g_status);
}

void StopEverything() {
    g_ctrlListener.Stop();
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

    // Matrix mode: the SCM started us with --run-candidate-matrix in the
    // ImagePath. Report SERVICE_RUNNING BEFORE running the matrix (which takes
    // 1-2 seconds) so the SCM and Start-Service see the service in the Running
    // state before it stops. Reporting SERVICE_RUNNING after the matrix causes
    // Start-Service to see an instantaneous Running→Stopped transition and
    // throw "Cannot start service".
    bool matrixMode = false;
    for (int i = 1; i < g_argc; ++i) {
        if (std::wcscmp(g_argv[i], L"--run-candidate-matrix") == 0) {
            matrixMode = true;
            break;
        }
    }
    if (matrixMode) {
        SetStatus(SERVICE_RUNNING);
        RunCandidateMatrix(g_argc, g_argv);
        SetStatus(SERVICE_STOPPED, NO_ERROR);
        return;
    }

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

    // Task 2.3: record the verified token (account, groups, privilege state)
    // that this process's raw-volume calls run under, so an installer-driven
    // restart with a freshly granted token is auditable in the diagnostic
    // record rather than assumed.
    ffindexsvc::LogStartupTokenDiagnostic();

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

    if (!ctrlStarted) {
        StopEverything();
        SetStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetStatus(SERVICE_RUNNING);
    ffindexsvc::WriteServiceLifecycleLog(L"service started");
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    g_argc = argc;
    g_argv = argv;

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(ffsetup::kServiceName), ServiceMain},
        {nullptr, nullptr},
    };

    // When started by the SCM, StartServiceCtrlDispatcherW blocks until
    // ServiceMain returns. ServiceMain detects --run-candidate-matrix via
    // g_argc/g_argv and runs the matrix row with a proper SCM handshake
    // (SERVICE_START_PENDING -> SERVICE_RUNNING -> SERVICE_STOPPED).
    if (StartServiceCtrlDispatcherW(table)) {
        return 0;
    }

    // Console mode (not started by the SCM): if in matrix mode, run directly
    // so the smoke test (--run-candidate-matrix from the command line) works.
    if (RunCandidateMatrix(argc, argv)) {
        return 0;
    }

    return 1;
}
