// FastFilesIndexSvc: the privileged, stateless Windows Service.
//
// This is the buildable skeleton for the process (task 1.2). The real
// service registration, IPC handshake, and volume/journal command
// handling (tasks 3.1-3.12) land in a follow-up pass on this same
// change -- see openspec/changes/establish-architecture-foundation.
#include <windows.h>

namespace {

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;

void WINAPI ServiceCtrlHandler(DWORD control) {
    if (control == SERVICE_CONTROL_STOP) {
        g_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_statusHandle, &g_status);
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerW(L"FastFilesIndexSvc", ServiceCtrlHandler);
    if (g_statusHandle == nullptr) {
        return;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = SERVICE_RUNNING;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(g_statusHandle, &g_status);
}

} // namespace

int wmain() {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(L"FastFilesIndexSvc"), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}
